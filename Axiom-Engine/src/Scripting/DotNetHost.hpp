#pragma once
#include <string>
#include <filesystem>

// Forward-declare hostfxr types so we don't pull in the header here
typedef void* hostfxr_handle;

namespace Axiom {

#ifdef AIM_PLATFORM_WINDOWS
	using DotNetHostChar = wchar_t;
#define AXIOM_DOTNET_STR(s) L ## s
#else
	using DotNetHostChar = char;
#define AXIOM_DOTNET_STR(s) s
#endif

#define AXIOM_DOTNET_UNMANAGEDCALLERSONLY_METHOD reinterpret_cast<const Axiom::DotNetHostChar*>(-1)

	/// <summary>
	/// Encapsulates CoreCLR initialization via the hostfxr/.NET hosting API.
	///
	/// Usage:
	///   1. Initialize(runtimeConfigPath) — boots the .NET runtime
	///   2. LoadAssemblyAndGetFunction(...) — loads an assembly and gets a function pointer
	///   3. Close() — shuts down the runtime
	/// </summary>
	class DotNetHost {
	public:
		DotNetHost() = default;
		~DotNetHost();

		DotNetHost(const DotNetHost&) = delete;
		DotNetHost& operator=(const DotNetHost&) = delete;

		/// Initialize the .NET runtime using the given .runtimeconfig.json path.
		bool Initialize(const std::filesystem::path& runtimeConfigPath);

		/// Load a managed assembly and get a function pointer to a specific method.
		/// typeName: "Namespace.TypeName, AssemblyName" (assembly-qualified)
		/// methodName: method name (e.g. "Initialize")
		/// delegateType: UNMANAGEDCALLERSONLY_METHOD or a specific delegate type name
		bool LoadAssemblyAndGetFunction(
			const std::filesystem::path& assemblyPath,
			const DotNetHostChar* typeName,
			const DotNetHostChar* methodName,
			const DotNetHostChar* delegateType,
			void** outFunctionPtr);

		/// Shut down the .NET runtime.
		void Close();

		bool IsInitialized() const { return m_Initialized; }

	private:
		bool LoadHostFxr();

		// hostfxr function pointers (loaded dynamically)
		void* m_HostFxrLib = nullptr;  // HMODULE
		hostfxr_handle m_HostContext = nullptr;
		bool m_Initialized = false;

		// Cached hostfxr functions
		void* m_InitFn = nullptr;   // hostfxr_initialize_for_runtime_config_fn
		void* m_GetDelegateFn = nullptr;  // hostfxr_get_runtime_delegate_fn
		void* m_CloseFn = nullptr;  // hostfxr_close_fn
		void* m_SetErrorWriterFn = nullptr;

		// Cached runtime delegate
		void* m_LoadAssemblyAndGetFunctionPointer = nullptr;
	};

} // namespace Axiom
