#include "pch.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scene/Scene.hpp"
#include "Core/Log.hpp"

#include <algorithm>
#include <filesystem>

// From coreclr_delegates.h — signals [UnmanagedCallersOnly] to hostfxr
#ifndef UNMANAGEDCALLERSONLY_METHOD
#define UNMANAGEDCALLERSONLY_METHOD ((const wchar_t*)-1)
#endif

namespace Axiom {

	bool        ScriptEngine::s_Initialized = false;
	Scene*      ScriptEngine::s_CurrentScene = nullptr;
	std::string ScriptEngine::s_CoreAssemblyPath;
	std::string ScriptEngine::s_UserAssemblyPath;
	bool        ScriptEngine::s_HasUserAssembly = false;
	std::vector<ScriptEngine::GlobalSystemInstance> ScriptEngine::s_GlobalSystems;

	DotNetHost       ScriptEngine::s_Host;
	ManagedCallbacks ScriptEngine::s_Callbacks{};

	void ScriptEngine::Init()
	{
		if (s_Initialized)
			return;

		s_Initialized = false;
		AIM_CORE_INFO_TAG("ScriptEngine", "ScriptEngine ready (CoreCLR, deferred init)");
	}

	void ScriptEngine::Shutdown()
	{
		ShutdownGlobalSystems();

		// Do NOT close the host — CoreCLR can only be initialized once per process.
		// Just unload user assemblies and reset state.
		if (s_Callbacks.UnloadUserAssembly)
			s_Callbacks.UnloadUserAssembly();

		s_Initialized = false;
		s_HasUserAssembly = false;
		s_Callbacks = {};
		AIM_CORE_INFO_TAG("ScriptEngine", "Script engine shut down (host stays alive)");
	}

	bool ScriptEngine::IsInitialized()
	{
		return s_Initialized;
	}

	void ScriptEngine::LoadCoreAssembly(const std::string& path)
	{
		s_CoreAssemblyPath = path;

		if (!std::filesystem::exists(path))
		{
			AIM_CORE_ERROR_TAG("ScriptEngine", "Assembly not found: {}", path);
			return;
		}
		auto asmPath = std::filesystem::canonical(std::filesystem::path(path));
		auto configPath = asmPath.parent_path() / (asmPath.stem().string() + ".runtimeconfig.json");
		AIM_CORE_INFO_TAG("ScriptEngine", "Assembly: {}", asmPath.string());
		AIM_CORE_INFO_TAG("ScriptEngine", "RuntimeConfig: {}", configPath.string());

		if (!std::filesystem::exists(configPath))
		{
			AIM_CORE_ERROR_TAG("ScriptEngine", "Runtime config not found: {}", configPath.string());
			return;
		}

		if (!s_Host.IsInitialized())
		{
			if (!s_Host.Initialize(configPath))
			{
				AIM_CORE_ERROR_TAG("ScriptEngine", "Failed to initialize CoreCLR runtime");
				return;
			}
		}

		using InitializeFn = int(*)(void* nativeBindings, void* managedCallbacks);
		InitializeFn initFn = nullptr;

		bool ok = s_Host.LoadAssemblyAndGetFunction(
			asmPath,
			L"Axiom.Interop.ScriptHostBridge, Axiom-ScriptCore",
			L"Initialize",
			UNMANAGEDCALLERSONLY_METHOD,
			reinterpret_cast<void**>(&initFn));

		if (!ok || !initFn)
		{
			AIM_CORE_ERROR_TAG("ScriptEngine", "Failed to get ScriptHostBridge.Initialize");
			return;
		}

		NativeBindings nativeBindings{};
		ScriptBindings::PopulateNativeBindings(nativeBindings);

		ManagedCallbacks managedCallbacks{};
		int result = initFn(&nativeBindings, &managedCallbacks);

		if (result != 0)
		{
			AIM_CORE_ERROR_TAG("ScriptEngine", "ScriptHostBridge.Initialize returned error: {}", result);
			return;
		}

		s_Callbacks = managedCallbacks;
		s_Initialized = true;
		AIM_CORE_INFO_TAG("ScriptEngine", "Core assembly loaded: {}", path);
	}

	void ScriptEngine::LoadUserAssembly(const std::string& path)
	{
		if (!s_Initialized)
		{
			AIM_CORE_ERROR_TAG("ScriptEngine", "Cannot load user assembly: engine not initialized");
			return;
		}

		if (!std::filesystem::exists(path))
		{
			AIM_CORE_WARN_TAG("ScriptEngine", "User assembly not found: {}", path);
			return;
		}

		auto canonPath = std::filesystem::canonical(std::filesystem::path(path));
		s_UserAssemblyPath = canonPath.string();
		AIM_CORE_INFO_TAG("ScriptEngine", "Loading user assembly: {}", s_UserAssemblyPath);

		if (s_Callbacks.LoadUserAssembly)
		{
			ShutdownGlobalSystems();
			int ok = s_Callbacks.LoadUserAssembly(s_UserAssemblyPath.c_str());
			AIM_CORE_INFO_TAG("ScriptEngine", "LoadUserAssembly callback returned: {}", ok);
			s_HasUserAssembly = (ok != 0);

			if (s_HasUserAssembly)
				AIM_CORE_INFO_TAG("ScriptEngine", "User assembly loaded: {}", path);
			else
				AIM_CORE_ERROR_TAG("ScriptEngine", "Failed to load user assembly: {}", path);
		}
	}

	bool ScriptEngine::HasUserAssembly()
	{
		return s_HasUserAssembly;
	}

	void ScriptEngine::ReloadAssemblies()
	{
		AIM_CORE_INFO_TAG("ScriptEngine", "Reloading assemblies...");

		ShutdownGlobalSystems();

		if (s_Callbacks.UnloadUserAssembly)
			s_Callbacks.UnloadUserAssembly();

		s_HasUserAssembly = false;

		if (!s_UserAssemblyPath.empty())
			LoadUserAssembly(s_UserAssemblyPath);
	}

	void ScriptEngine::SetScene(Scene* scene)
	{
		s_CurrentScene = scene;
	}

	Scene* ScriptEngine::GetScene()
	{
		return s_CurrentScene;
	}

	uint32_t ScriptEngine::CreateScriptInstance(const std::string& className, EntityHandle entity)
	{
		if (!s_Initialized || !s_Callbacks.CreateScriptInstance)
		{
			AIM_CORE_ERROR_TAG("ScriptEngine", "Cannot create instance: engine not ready");
			return 0;
		}

		uint64_t entityID = static_cast<uint64_t>(static_cast<uint32_t>(entity));
		if (s_CurrentScene && s_CurrentScene->IsValid(entity)) {
			const uint64_t runtimeId = s_CurrentScene->GetRuntimeID(entity);
			if (runtimeId != 0) {
				entityID = runtimeId;
			}
		}

		return static_cast<uint32_t>(s_Callbacks.CreateScriptInstance(className.c_str(), entityID));
	}

	void ScriptEngine::DestroyScriptInstance(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.DestroyScriptInstance) return;
		s_Callbacks.DestroyScriptInstance(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeStart(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeStart) return;
		s_Callbacks.InvokeStart(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeUpdate(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeUpdate) return;
		s_Callbacks.InvokeUpdate(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeOnDestroy(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeOnDestroy) return;
		s_Callbacks.InvokeOnDestroy(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeOnEnable(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeOnEnable) return;
		s_Callbacks.InvokeOnEnable(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeOnDisable(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeOnDisable) return;
		s_Callbacks.InvokeOnDisable(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeCollisionEnter2D(uint32_t handle, uint64_t selfEntityID, uint64_t otherEntityID, uint64_t entityAID, uint64_t entityBID, float contactPointX, float contactPointY)
	{
		if (handle == 0 || !s_Callbacks.InvokeCollisionEnter2D) return;
		s_Callbacks.InvokeCollisionEnter2D(static_cast<int32_t>(handle), selfEntityID, otherEntityID, entityAID, entityBID, contactPointX, contactPointY);
	}

	void ScriptEngine::InvokeCollisionStay2D(uint32_t handle, uint64_t selfEntityID, uint64_t otherEntityID, uint64_t entityAID, uint64_t entityBID, float contactPointX, float contactPointY)
	{
		if (handle == 0 || !s_Callbacks.InvokeCollisionStay2D) return;
		s_Callbacks.InvokeCollisionStay2D(static_cast<int32_t>(handle), selfEntityID, otherEntityID, entityAID, entityBID, contactPointX, contactPointY);
	}

	void ScriptEngine::InvokeCollisionExit2D(uint32_t handle, uint64_t selfEntityID, uint64_t otherEntityID, uint64_t entityAID, uint64_t entityBID, float contactPointX, float contactPointY)
	{
		if (handle == 0 || !s_Callbacks.InvokeCollisionExit2D) return;
		s_Callbacks.InvokeCollisionExit2D(static_cast<int32_t>(handle), selfEntityID, otherEntityID, entityAID, entityBID, contactPointX, contactPointY);
	}

	bool ScriptEngine::ClassExists(const std::string& className)
	{
		if (!s_Initialized || !s_Callbacks.ClassExists) return false;
		return s_Callbacks.ClassExists(className.c_str()) != 0;
	}

	void ScriptEngine::RaiseApplicationStart()
	{
		if (s_Initialized && s_Callbacks.RaiseApplicationStart) s_Callbacks.RaiseApplicationStart();
	}

	void ScriptEngine::RaiseApplicationPaused()
	{
		if (s_Initialized && s_Callbacks.RaiseApplicationPaused) s_Callbacks.RaiseApplicationPaused();
	}

	void ScriptEngine::RaiseApplicationQuit()
	{
		if (s_Initialized && s_Callbacks.RaiseApplicationQuit) s_Callbacks.RaiseApplicationQuit();
	}

	void ScriptEngine::RaiseFocusChanged(bool focused)
	{
		if (s_Initialized && s_Callbacks.RaiseFocusChanged) s_Callbacks.RaiseFocusChanged(focused ? 1 : 0);
	}

	void ScriptEngine::RaiseKeyDown(int key)
	{
		if (s_Initialized && s_Callbacks.RaiseKeyDown) s_Callbacks.RaiseKeyDown(key);
	}

	void ScriptEngine::RaiseKeyUp(int key)
	{
		if (s_Initialized && s_Callbacks.RaiseKeyUp) s_Callbacks.RaiseKeyUp(key);
	}

	void ScriptEngine::RaiseMouseDown(int button)
	{
		if (s_Initialized && s_Callbacks.RaiseMouseDown) s_Callbacks.RaiseMouseDown(button);
	}

	void ScriptEngine::RaiseMouseUp(int button)
	{
		if (s_Initialized && s_Callbacks.RaiseMouseUp) s_Callbacks.RaiseMouseUp(button);
	}

	void ScriptEngine::RaiseMouseScroll(float delta)
	{
		if (s_Initialized && s_Callbacks.RaiseMouseScroll) s_Callbacks.RaiseMouseScroll(delta);
	}

	void ScriptEngine::RaiseMouseMove(float x, float y)
	{
		if (s_Initialized && s_Callbacks.RaiseMouseMove) s_Callbacks.RaiseMouseMove(x, y);
	}

	void ScriptEngine::RaiseBeforeSceneLoaded(const std::string& sceneName)
	{
		if (s_Initialized && s_Callbacks.RaiseBeforeSceneLoaded) s_Callbacks.RaiseBeforeSceneLoaded(sceneName.c_str());
	}

	void ScriptEngine::RaiseSceneLoaded(const std::string& sceneName)
	{
		if (s_Initialized && s_Callbacks.RaiseSceneLoaded) s_Callbacks.RaiseSceneLoaded(sceneName.c_str());
	}

	void ScriptEngine::RaiseBeforeSceneUnloaded(const std::string& sceneName)
	{
		if (s_Initialized && s_Callbacks.RaiseBeforeSceneUnloaded) s_Callbacks.RaiseBeforeSceneUnloaded(sceneName.c_str());
	}

	void ScriptEngine::RaiseSceneUnloaded(const std::string& sceneName)
	{
		if (s_Initialized && s_Callbacks.RaiseSceneUnloaded) s_Callbacks.RaiseSceneUnloaded(sceneName.c_str());
	}

	uint32_t ScriptEngine::CreateGameSystemInstance(const std::string& className, const std::string& sceneName)
	{
		if (!s_Initialized || !s_Callbacks.CreateGameSystemInstance) return 0;
		return static_cast<uint32_t>(s_Callbacks.CreateGameSystemInstance(className.c_str(), sceneName.c_str()));
	}

	void ScriptEngine::DestroyGameSystemInstance(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.DestroyGameSystemInstance) return;
		s_Callbacks.DestroyGameSystemInstance(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeGameSystemStart(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeGameSystemStart) return;
		s_Callbacks.InvokeGameSystemStart(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeGameSystemUpdate(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeGameSystemUpdate) return;
		s_Callbacks.InvokeGameSystemUpdate(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeGameSystemEnable(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeGameSystemEnable) return;
		s_Callbacks.InvokeGameSystemEnable(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeGameSystemDisable(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeGameSystemDisable) return;
		s_Callbacks.InvokeGameSystemDisable(static_cast<int32_t>(handle));
	}

	void ScriptEngine::InvokeGameSystemDestroy(uint32_t handle)
	{
		if (handle == 0 || !s_Callbacks.InvokeGameSystemDestroy) return;
		s_Callbacks.InvokeGameSystemDestroy(static_cast<int32_t>(handle));
	}

	bool ScriptEngine::GameSystemClassExists(const std::string& className)
	{
		if (!s_Initialized || !s_Callbacks.GameSystemClassExists) return false;
		return s_Callbacks.GameSystemClassExists(className.c_str()) != 0;
	}

	void ScriptEngine::InitializeGlobalSystems(const std::vector<std::string>& classNames)
	{
		if (!s_Initialized || !s_HasUserAssembly || !s_Callbacks.CreateGlobalSystemInstance) return;

		for (const std::string& className : classNames)
		{
			if (className.empty()) {
				continue;
			}

			const auto existing = std::find_if(s_GlobalSystems.begin(), s_GlobalSystems.end(),
				[&className](const GlobalSystemInstance& instance) { return instance.ClassName == className; });
			if (existing != s_GlobalSystems.end()) {
				continue;
			}

			if (!GlobalSystemClassExists(className)) {
				AIM_CORE_WARN_TAG("ScriptEngine", "GlobalSystem '{}' was registered but no matching class was found", className);
				continue;
			}

			uint32_t handle = static_cast<uint32_t>(s_Callbacks.CreateGlobalSystemInstance(className.c_str()));
			if (handle == 0) {
				AIM_CORE_ERROR_TAG("ScriptEngine", "Failed to create GlobalSystem '{}'", className);
				continue;
			}

			s_GlobalSystems.push_back({ className, handle, true });
			if (s_Callbacks.InvokeGlobalSystemEnable) {
				s_Callbacks.InvokeGlobalSystemEnable(static_cast<int32_t>(handle));
			}
			if (s_Callbacks.InvokeGlobalSystemInitialize) {
				s_Callbacks.InvokeGlobalSystemInitialize(static_cast<int32_t>(handle));
			}
		}
	}

	void ScriptEngine::UpdateGlobalSystems()
	{
		if (!s_Initialized || !s_Callbacks.InvokeGlobalSystemUpdate) return;
		for (const auto& instance : s_GlobalSystems)
		{
			if (instance.Handle != 0 && instance.Enabled) {
				s_Callbacks.InvokeGlobalSystemUpdate(static_cast<int32_t>(instance.Handle));
			}
		}
	}

	void ScriptEngine::ShutdownGlobalSystems()
	{
		if (s_Callbacks.DestroyGlobalSystemInstance) {
			for (auto& instance : s_GlobalSystems)
			{
				if (instance.Handle != 0) {
					if (s_Callbacks.InvokeGlobalSystemDisable) {
						s_Callbacks.InvokeGlobalSystemDisable(static_cast<int32_t>(instance.Handle));
					}
					s_Callbacks.DestroyGlobalSystemInstance(static_cast<int32_t>(instance.Handle));
				}
			}
		}
		s_GlobalSystems.clear();
	}

	void ScriptEngine::SetGlobalSystemEnabled(const std::string& className, bool enabled)
	{
		if (!s_Initialized) return;

		auto it = std::find_if(s_GlobalSystems.begin(), s_GlobalSystems.end(),
			[&className](const GlobalSystemInstance& instance) { return instance.ClassName == className; });
		if (it == s_GlobalSystems.end() || it->Handle == 0) return;
		if (it->Enabled == enabled) return;

		it->Enabled = enabled;

		if (enabled) {
			if (s_Callbacks.InvokeGlobalSystemEnable) {
				s_Callbacks.InvokeGlobalSystemEnable(static_cast<int32_t>(it->Handle));
			}
		}
		else if (s_Callbacks.InvokeGlobalSystemDisable) {
			s_Callbacks.InvokeGlobalSystemDisable(static_cast<int32_t>(it->Handle));
		}
	}

	bool ScriptEngine::GlobalSystemClassExists(const std::string& className)
	{
		if (!s_Initialized || !s_Callbacks.GlobalSystemClassExists) return false;
		return s_Callbacks.GlobalSystemClassExists(className.c_str()) != 0;
	}

} // namespace Axiom
