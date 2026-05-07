#include "pch.hpp"
#include "Scripting/NativeScriptHost.hpp"
#include "Scripting/NativeScript.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Core/Log.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Scene/Scene.hpp"
#include "Components/General/Transform2DComponent.hpp"

#include <chrono>
#include <exception>

#if defined(AIM_PLATFORM_WINDOWS)
#include <windows.h>
#elif defined(AIM_PLATFORM_LINUX)
#include <dlfcn.h>
#endif

namespace Axiom {

	// Engine API implementations — these run IN the engine process,
	// called through function pointers from the DLL.

	static void API_LogInfo(const char* msg) {
		Log::PrintMessageTag(Log::Type::Client, Log::Level::Info, "NativeScript", msg);
	}
	static void API_LogWarn(const char* msg) {
		Log::PrintMessageTag(Log::Type::Client, Log::Level::Warn, "NativeScript", msg);
	}
	static void API_LogError(const char* msg) {
		Log::PrintMessageTag(Log::Type::Client, Log::Level::Error, "NativeScript", msg);
	}
	static float API_GetDeltaTime() {
		auto* app = Application::GetInstance();
		return app ? app->GetTime().GetDeltaTime() : 0.0f;
	}
	static void API_GetPosition(uint32_t entityID, float* x, float* y) {
		Scene* scene = ScriptEngine::GetScene();
		if (!scene) { *x = 0; *y = 0; return; }
		auto handle = static_cast<EntityHandle>(entityID);
		if (!scene->HasComponent<Transform2DComponent>(handle)) { *x = 0; *y = 0; return; }
		auto& t = scene->GetComponent<Transform2DComponent>(handle);
		*x = t.Position.x; *y = t.Position.y;
	}
	static void API_SetPosition(uint32_t entityID, float x, float y) {
		Scene* scene = ScriptEngine::GetScene();
		if (!scene) return;
		auto handle = static_cast<EntityHandle>(entityID);
		if (!scene->HasComponent<Transform2DComponent>(handle)) return;
		scene->GetComponent<Transform2DComponent>(handle).Position = { x, y };
	}
	static float API_GetRotation(uint32_t entityID) {
		Scene* scene = ScriptEngine::GetScene();
		if (!scene) return 0.0f;
		auto handle = static_cast<EntityHandle>(entityID);
		if (!scene->HasComponent<Transform2DComponent>(handle)) return 0.0f;
		return scene->GetComponent<Transform2DComponent>(handle).Rotation;
	}
	static void API_SetRotation(uint32_t entityID, float rot) {
		Scene* scene = ScriptEngine::GetScene();
		if (!scene) return;
		auto handle = static_cast<EntityHandle>(entityID);
		if (!scene->HasComponent<Transform2DComponent>(handle)) return;
		scene->GetComponent<Transform2DComponent>(handle).Rotation = rot;
	}

	static NativeEngineAPI s_EngineAPI = {
		API_LogInfo, API_LogWarn, API_LogError,
		API_GetDeltaTime,
		API_GetPosition, API_SetPosition,
		API_GetRotation, API_SetRotation
	};

	namespace {
		using NativeCreateFn = NativeScript* (*)(const char*);
		using NativeDestroyFn = void (*)(NativeScript*);
		using NativeHasFn = int (*)(const char*);
		using NativeInitFn = void (*)(void* engineAPI);

		std::filesystem::path NormalizeLibraryPath(const std::string& dllPath)
		{
			std::error_code ec;
			const std::filesystem::path sourcePath = std::filesystem::path(dllPath);
			if (std::filesystem::exists(sourcePath, ec)) {
				const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(sourcePath, ec);
				if (!ec) {
					return canonicalPath;
				}
			}

			return sourcePath.lexically_normal();
		}

		std::filesystem::path CreateShadowCopyPath(const std::filesystem::path& sourcePath)
		{
			std::error_code ec;
			std::filesystem::path shadowDirectory = std::filesystem::temp_directory_path(ec);
			if (ec || shadowDirectory.empty()) {
				shadowDirectory = sourcePath.parent_path();
			}

			shadowDirectory /= "AxiomNativeScriptHost";
			std::filesystem::create_directories(shadowDirectory, ec);

			const auto uniqueStamp = std::chrono::steady_clock::now().time_since_epoch().count();
			return shadowDirectory / (sourcePath.stem().string() + "-" + std::to_string(uniqueStamp) + sourcePath.extension().string());
		}

		void UnloadLibraryHandle(void* dllHandle)
		{
#if defined(AIM_PLATFORM_WINDOWS)
			if (dllHandle) {
				FreeLibrary(static_cast<HMODULE>(dllHandle));
			}
#elif defined(AIM_PLATFORM_LINUX)
			if (dllHandle) {
				dlclose(dllHandle);
			}
#else
			(void)dllHandle;
#endif
		}

		void RemoveShadowCopy(const std::filesystem::path& shadowPath)
		{
			if (shadowPath.empty()) {
				return;
			}

			std::error_code ec;
			std::filesystem::remove(shadowPath, ec);
		}

		// Renamed (was TryInvokeNativeHostCallback) to match what it actually does:
		// the body unconditionally called OnDestroy regardless of the passed
		// callback name, so the parameter was a misleading lie.
		bool TryInvokeOnDestroy(NativeScript* script)
		{
			try {
				script->OnDestroy();
				return true;
			}
			catch (const std::exception& e) {
				AIM_CORE_ERROR_TAG("NativeScriptHost", "Native script failed during OnDestroy: {}", e.what());
			}
			catch (...) {
				AIM_CORE_ERROR_TAG("NativeScriptHost", "Native script failed during OnDestroy with an unknown exception");
			}

			return false;
		}
	}

	bool NativeScriptHost::LoadDLL(const std::string& dllPath)
	{
		const std::filesystem::path sourcePath = NormalizeLibraryPath(dllPath);
		if (!std::filesystem::exists(sourcePath))
		{
			AIM_CORE_ERROR_TAG("NativeScriptHost", "Failed to load DLL: {}", sourcePath.string());
			return false;
		}

		// Reload sequences leak orphaned shadow copies if the previous run
		// crashed (UnloadDLL never ran) or if RemoveShadowCopy raced a Windows
		// file lock. Sweep stale copies sharing this DLL's stem before adding a
		// fresh one so the temp directory doesn't accumulate forever.
		{
			std::error_code sweepErr;
			std::filesystem::path shadowDirectory = std::filesystem::temp_directory_path(sweepErr);
			if (!sweepErr && !shadowDirectory.empty()) {
				shadowDirectory /= "AxiomNativeScriptHost";
				if (std::filesystem::exists(shadowDirectory, sweepErr)) {
					const std::string targetStem = sourcePath.stem().string();
					for (const auto& entry : std::filesystem::directory_iterator(shadowDirectory, sweepErr)) {
						if (sweepErr) break;
						const std::filesystem::path& entryPath = entry.path();
						if (entryPath == m_LoadedDllPath) {
							continue; // never delete the currently-loaded DLL
						}
						const std::string entryStem = entryPath.stem().string();
						// Stem looks like "<originalStem>-<timestamp>"; match prefix.
						if (entryStem.rfind(targetStem + "-", 0) != 0) {
							continue;
						}
						std::error_code removeErr;
						std::filesystem::remove(entryPath, removeErr);
						// Best-effort: ignore failures (file still locked by another process).
					}
				}
			}
		}

		const std::filesystem::path shadowPath = CreateShadowCopyPath(sourcePath);
		std::error_code copyError;
		std::filesystem::copy_file(sourcePath, shadowPath, std::filesystem::copy_options::overwrite_existing, copyError);
		if (copyError)
		{
			AIM_CORE_ERROR_TAG("NativeScriptHost", "Failed to create DLL shadow copy '{}': {}", shadowPath.string(), copyError.message());
			return false;
		}

		void* newDllHandle = nullptr;

#if defined(AIM_PLATFORM_WINDOWS)
		newDllHandle = static_cast<void*>(LoadLibraryA(shadowPath.string().c_str()));
		if (!newDllHandle)
		{
			AIM_CORE_ERROR_TAG("NativeScriptHost", "Failed to load DLL: {}", sourcePath.string());
			RemoveShadowCopy(shadowPath);
			return false;
		}
#elif defined(AIM_PLATFORM_LINUX)
		dlerror();
		newDllHandle = dlopen(shadowPath.string().c_str(), RTLD_NOW);
		if (!newDllHandle)
		{
			const char* error = dlerror();
			AIM_CORE_ERROR_TAG("NativeScriptHost", "Failed to load shared library '{}': {}", sourcePath.string(), error ? error : "unknown error");
			RemoveShadowCopy(shadowPath);
			return false;
		}
#else
		AIM_CORE_ERROR_TAG("NativeScriptHost", "Native scripts are not supported on this platform");
		RemoveShadowCopy(shadowPath);
		return false;
#endif

		auto getSymbol = [newDllHandle](const char* name) -> void* {
#if defined(AIM_PLATFORM_WINDOWS)
			return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(newDllHandle), name));
#elif defined(AIM_PLATFORM_LINUX)
			dlerror();
			return dlsym(newDllHandle, name);
#else
			return nullptr;
#endif
		};

		NativeCreateFn newCreateFn = reinterpret_cast<NativeCreateFn>(getSymbol("AxiomCreateScript"));
		NativeDestroyFn newDestroyFn = reinterpret_cast<NativeDestroyFn>(getSymbol("AxiomDestroyScript"));
		NativeHasFn newHasFn = reinterpret_cast<NativeHasFn>(getSymbol("AxiomHasScript"));

		if (!newCreateFn || !newDestroyFn)
		{
			AIM_CORE_ERROR_TAG("NativeScriptHost",
				"DLL missing AxiomCreateScript/AxiomDestroyScript: {}", sourcePath.string());
			UnloadLibraryHandle(newDllHandle);
			RemoveShadowCopy(shadowPath);
			return false;
		}

		// Pass engine API to the DLL so scripts can call engine functions
		NativeInitFn initFn = reinterpret_cast<NativeInitFn>(getSymbol("AxiomInitialize"));
		if (initFn)
			initFn(&s_EngineAPI);

		UnloadDLL();

		m_DllHandle = newDllHandle;
		m_CreateFn = newCreateFn;
		m_DestroyFn = newDestroyFn;
		m_HasFn = newHasFn;
		m_DllPath = sourcePath.string();
		m_LoadedDllPath = shadowPath;

		AIM_CORE_INFO_TAG("NativeScriptHost", "Loaded native script DLL: {}", sourcePath.string());
		return true;
	}

	void NativeScriptHost::UnloadDLL()
	{
		DestroyAllInstances();

		UnloadLibraryHandle(m_DllHandle);
		RemoveShadowCopy(m_LoadedDllPath);
		m_DllHandle = nullptr;
		m_CreateFn = nullptr;
		m_DestroyFn = nullptr;
		m_HasFn = nullptr;
		m_LoadedDllPath.clear();
	}

	bool NativeScriptHost::Reload()
	{
		if (m_DllPath.empty()) return false;
		return LoadDLL(m_DllPath);
	}

	NativeScript* NativeScriptHost::CreateInstance(
		const std::string& className, EntityHandle entity, Scene* scene)
	{
		if (!m_CreateFn) return nullptr;

		NativeScript* script = m_CreateFn(className.c_str());
		if (!script) return nullptr;

		script->m_EntityID = static_cast<uint32_t>(entity);
		script->m_Entity = entity;
		script->m_Scene = scene;
		m_LiveInstances.push_back(script);
		return script;
	}

	void NativeScriptHost::DestroyInstance(NativeScript* script)
	{
		if (!script) return;
		TryInvokeOnDestroy(script);

		auto it = std::find(m_LiveInstances.begin(), m_LiveInstances.end(), script);
		if (it != m_LiveInstances.end())
			m_LiveInstances.erase(it);

		script->m_EntityID = 0;
		script->m_Entity = entt::null;
		script->m_Scene = nullptr;

		if (m_DestroyFn)
			m_DestroyFn(script);
	}

	void NativeScriptHost::DestroyAllInstances()
	{
		// Snapshot the live-instance pointers and clear the master vector BEFORE
		// invoking any user OnDestroy. A user OnDestroy can re-enter and call
		// DestroyInstance(other), which erases from m_LiveInstances mid-iteration
		// — corrupting our walk. By moving out the vector first, the re-entrant
		// erase becomes a no-op (the snapshot is independent), and we still
		// guarantee that every live instance gets exactly one OnDestroy + DestroyFn.
		std::vector<NativeScript*> snapshot = std::move(m_LiveInstances);
		m_LiveInstances.clear();

		for (auto* script : snapshot)
		{
			if (!script) continue;
			TryInvokeOnDestroy(script);
			script->m_EntityID = 0;
			script->m_Entity = entt::null;
			script->m_Scene = nullptr;
			if (m_DestroyFn) m_DestroyFn(script);
		}
	}

	bool NativeScriptHost::HasClass(const std::string& className)
	{
		if (!m_CreateFn) return false;
		if (m_HasFn) return m_HasFn(className.c_str()) != 0;

		NativeScript* test = m_CreateFn(className.c_str());
		if (!test) return false;
		if (m_DestroyFn) m_DestroyFn(test);
		return true;
	}

} // namespace Axiom
