#include "pch.hpp"
#include "Scripting/DotNetHost.hpp"
#include "Core/Log.hpp"

#include <nethost.h>
#include <hostfxr.h>
#include <coreclr_delegates.h>

#ifdef BT_PLATFORM_WINDOWS
#include <windows.h>
#define STR(s) L ## s
#else
#include <dlfcn.h>
#define STR(s) s
#endif

namespace Bolt {

	// ── Error callback ─────────────────────────────────────────────────

	static void HOSTFXR_CALLTYPE HostFxrErrorWriter(const char_t* message)
	{
#ifdef BT_PLATFORM_WINDOWS
		// Convert wchar_t* to UTF-8 for logging
		int size = WideCharToMultiByte(CP_UTF8, 0, message, -1, nullptr, 0, nullptr, nullptr);
		std::string utf8(size - 1, 0);
		WideCharToMultiByte(CP_UTF8, 0, message, -1, &utf8[0], size, nullptr, nullptr);
		BT_CORE_ERROR_TAG("DotNetHost", "{}", utf8);
#else
		BT_CORE_ERROR_TAG("DotNetHost", "{}", message);
#endif
	}

	// ── Platform helpers ───────────────────────────────────────────────

	static void* LoadLibraryFromPath(const char_t* path)
	{
#ifdef BT_PLATFORM_WINDOWS
		return static_cast<void*>(LoadLibraryW(path));
#else
		return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
#endif
	}

	static void* GetExport(void* lib, const char* name)
	{
#ifdef BT_PLATFORM_WINDOWS
		return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), name));
#else
		return dlsym(lib, name);
#endif
	}

	static void FreeLibrary(void* lib)
	{
#ifdef BT_PLATFORM_WINDOWS
		::FreeLibrary(static_cast<HMODULE>(lib));
#else
		dlclose(lib);
#endif
	}

	// ── DotNetHost implementation ──────────────────────────────────────

	DotNetHost::~DotNetHost()
	{
		Close();
	}

	bool DotNetHost::LoadHostFxr()
	{
		if (m_HostFxrLib || m_HostContext || m_Initialized) {
			Close();
		}

		// Use nethost to find hostfxr.dll
		char_t hostfxrPath[1024]{};
		size_t pathSize = sizeof(hostfxrPath) / sizeof(char_t);

		int rc = get_hostfxr_path(hostfxrPath, &pathSize, nullptr);
		if (rc != 0)
		{
			BT_CORE_ERROR_TAG("DotNetHost", "Failed to find hostfxr: error 0x{:x}", static_cast<unsigned>(rc));
			return false;
		}

		// Load hostfxr library
		m_HostFxrLib = LoadLibraryFromPath(hostfxrPath);
		if (!m_HostFxrLib)
		{
			BT_CORE_ERROR_TAG("DotNetHost", "Failed to load hostfxr library");
			return false;
		}

		// Resolve function pointers
		m_InitFn = GetExport(m_HostFxrLib, "hostfxr_initialize_for_runtime_config");
		m_GetDelegateFn = GetExport(m_HostFxrLib, "hostfxr_get_runtime_delegate");
		m_CloseFn = GetExport(m_HostFxrLib, "hostfxr_close");
		m_SetErrorWriterFn = GetExport(m_HostFxrLib, "hostfxr_set_error_writer");

		if (!m_InitFn || !m_GetDelegateFn || !m_CloseFn)
		{
			BT_CORE_ERROR_TAG("DotNetHost", "Failed to resolve hostfxr exports");
			Close();
			return false;
		}

		return true;
	}

	bool DotNetHost::Initialize(const std::filesystem::path& runtimeConfigPath)
	{
		if (m_Initialized)
		{
			BT_CORE_WARN_TAG("DotNetHost", "Already initialized");
			return true;
		}

		// Step 1: Load hostfxr
		if (!LoadHostFxr())
			return false;

		// Set error writer for detailed diagnostics
		if (m_SetErrorWriterFn)
		{
			auto setErrorWriter = reinterpret_cast<hostfxr_set_error_writer_fn>(m_SetErrorWriterFn);
			setErrorWriter(HostFxrErrorWriter);
		}

		// Step 2: Initialize the .NET runtime
		auto initFn = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(m_InitFn);
		std::wstring configPath = runtimeConfigPath.wstring();

		int rc = initFn(configPath.c_str(), nullptr, &m_HostContext);
		// rc == 0: Success
		// rc == 1: Success_HostAlreadyInitialized (also valid)
		// rc == 2: Success_DifferentRuntimeProperties (also valid)
		if (rc < 0 || !m_HostContext)
		{
			BT_CORE_ERROR_TAG("DotNetHost", "Failed to initialize .NET runtime: 0x{:x}", static_cast<unsigned>(rc));
			Close();
			return false;
		}

		// Step 3: Get the load_assembly_and_get_function_pointer delegate
		auto getDelegateFn = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(m_GetDelegateFn);

		rc = getDelegateFn(m_HostContext,
			hdt_load_assembly_and_get_function_pointer,
			&m_LoadAssemblyAndGetFunctionPointer);

		if (rc != 0 || !m_LoadAssemblyAndGetFunctionPointer)
		{
			BT_CORE_ERROR_TAG("DotNetHost", "Failed to get load_assembly delegate: 0x{:x}", static_cast<unsigned>(rc));
			Close();
			return false;
		}

		m_Initialized = true;
		BT_CORE_INFO_TAG("DotNetHost", "CoreCLR runtime initialized successfully");
		return true;
	}

	bool DotNetHost::LoadAssemblyAndGetFunction(
		const std::filesystem::path& assemblyPath,
		const wchar_t* typeName,
		const wchar_t* methodName,
		const wchar_t* delegateType,
		void** outFunctionPtr)
	{
		if (!m_Initialized || !m_LoadAssemblyAndGetFunctionPointer)
		{
			BT_CORE_ERROR_TAG("DotNetHost", "Runtime not initialized");
			return false;
		}

		auto loadFn = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(
			m_LoadAssemblyAndGetFunctionPointer);

		// hostfxr requires canonical absolute paths
		if (!std::filesystem::exists(assemblyPath))
		{
			BT_CORE_ERROR_TAG("DotNetHost", "Assembly path does not exist: {}", assemblyPath.string());
			return false;
		}
		auto canonPath = std::filesystem::canonical(assemblyPath);
		std::wstring asmPath = canonPath.wstring();

		int rc = loadFn(
			asmPath.c_str(),
			typeName,
			methodName,
			delegateType,
			nullptr,       // reserved
			outFunctionPtr
		);

		if (rc != 0 || !*outFunctionPtr)
		{
			BT_CORE_ERROR_TAG("DotNetHost", "Failed to load assembly function: 0x{:x}", static_cast<unsigned>(rc));
			return false;
		}

		return true;
	}

	void DotNetHost::Close()
	{
		if (m_HostContext && m_CloseFn)
		{
			auto closeFn = reinterpret_cast<hostfxr_close_fn>(m_CloseFn);
			closeFn(m_HostContext);
			m_HostContext = nullptr;
		}

		if (m_HostFxrLib)
		{
			Bolt::FreeLibrary(m_HostFxrLib);
			m_HostFxrLib = nullptr;
		}

		m_Initialized = false;
		m_LoadAssemblyAndGetFunctionPointer = nullptr;
		m_InitFn = nullptr;
		m_GetDelegateFn = nullptr;
		m_CloseFn = nullptr;
		m_SetErrorWriterFn = nullptr;
	}

} // namespace Bolt
