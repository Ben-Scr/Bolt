#include "pch.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scene/Scene.hpp"
#include "Components/General/UUIDComponent.hpp"
#include "Core/Log.hpp"

#include <filesystem>

// From coreclr_delegates.h — signals [UnmanagedCallersOnly] to hostfxr
#ifndef UNMANAGEDCALLERSONLY_METHOD
#define UNMANAGEDCALLERSONLY_METHOD ((const wchar_t*)-1)
#endif

namespace Bolt {

	bool        ScriptEngine::s_Initialized = false;
	Scene*      ScriptEngine::s_CurrentScene = nullptr;
	std::string ScriptEngine::s_CoreAssemblyPath;
	std::string ScriptEngine::s_UserAssemblyPath;
	bool        ScriptEngine::s_HasUserAssembly = false;

	DotNetHost       ScriptEngine::s_Host;
	ManagedCallbacks ScriptEngine::s_Callbacks{};

	void ScriptEngine::Init()
	{
		if (s_Initialized)
			return;

		s_Initialized = false;
		BT_CORE_INFO_TAG("ScriptEngine", "ScriptEngine ready (CoreCLR, deferred init)");
	}

	void ScriptEngine::Shutdown()
	{
		// Do NOT close the host — CoreCLR can only be initialized once per process.
		// Just unload user assemblies and reset state.
		if (s_Callbacks.UnloadUserAssembly)
			s_Callbacks.UnloadUserAssembly();

		s_Initialized = false;
		s_HasUserAssembly = false;
		s_Callbacks = {};
		BT_CORE_INFO_TAG("ScriptEngine", "Script engine shut down (host stays alive)");
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
			BT_CORE_ERROR_TAG("ScriptEngine", "Assembly not found: {}", path);
			return;
		}
		auto asmPath = std::filesystem::canonical(std::filesystem::path(path));
		auto configPath = asmPath.parent_path() / (asmPath.stem().string() + ".runtimeconfig.json");
		BT_CORE_INFO_TAG("ScriptEngine", "Assembly: {}", asmPath.string());
		BT_CORE_INFO_TAG("ScriptEngine", "RuntimeConfig: {}", configPath.string());

		if (!std::filesystem::exists(configPath))
		{
			BT_CORE_ERROR_TAG("ScriptEngine", "Runtime config not found: {}", configPath.string());
			return;
		}

		if (!s_Host.IsInitialized())
		{
			if (!s_Host.Initialize(configPath))
			{
				BT_CORE_ERROR_TAG("ScriptEngine", "Failed to initialize CoreCLR runtime");
				return;
			}
		}

		using InitializeFn = int(*)(void* nativeBindings, void* managedCallbacks);
		InitializeFn initFn = nullptr;

		bool ok = s_Host.LoadAssemblyAndGetFunction(
			asmPath,
			L"Bolt.Hosting.ScriptHostBridge, Bolt-ScriptCore",
			L"Initialize",
			UNMANAGEDCALLERSONLY_METHOD,
			reinterpret_cast<void**>(&initFn));

		if (!ok || !initFn)
		{
			BT_CORE_ERROR_TAG("ScriptEngine", "Failed to get ScriptHostBridge.Initialize");
			return;
		}

		NativeBindings nativeBindings{};
		ScriptBindings::PopulateNativeBindings(nativeBindings);

		ManagedCallbacks managedCallbacks{};
		int result = initFn(&nativeBindings, &managedCallbacks);

		if (result != 0)
		{
			BT_CORE_ERROR_TAG("ScriptEngine", "ScriptHostBridge.Initialize returned error: {}", result);
			return;
		}

		s_Callbacks = managedCallbacks;
		s_Initialized = true;
		BT_CORE_INFO_TAG("ScriptEngine", "Core assembly loaded: {}", path);
	}

	void ScriptEngine::LoadUserAssembly(const std::string& path)
	{
		if (!s_Initialized)
		{
			BT_CORE_ERROR_TAG("ScriptEngine", "Cannot load user assembly: engine not initialized");
			return;
		}

		if (!std::filesystem::exists(path))
		{
			BT_CORE_WARN_TAG("ScriptEngine", "User assembly not found: {}", path);
			return;
		}

		auto canonPath = std::filesystem::canonical(std::filesystem::path(path));
		s_UserAssemblyPath = canonPath.string();
		BT_CORE_INFO_TAG("ScriptEngine", "Loading user assembly: {}", s_UserAssemblyPath);

		if (s_Callbacks.LoadUserAssembly)
		{
			int ok = s_Callbacks.LoadUserAssembly(s_UserAssemblyPath.c_str());
			BT_CORE_INFO_TAG("ScriptEngine", "LoadUserAssembly callback returned: {}", ok);
			s_HasUserAssembly = (ok != 0);

			if (s_HasUserAssembly)
				BT_CORE_INFO_TAG("ScriptEngine", "User assembly loaded: {}", path);
			else
				BT_CORE_ERROR_TAG("ScriptEngine", "Failed to load user assembly: {}", path);
		}
	}

	bool ScriptEngine::HasUserAssembly()
	{
		return s_HasUserAssembly;
	}

	void ScriptEngine::ReloadAssemblies()
	{
		BT_CORE_INFO_TAG("ScriptEngine", "Reloading assemblies...");

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
			BT_CORE_ERROR_TAG("ScriptEngine", "Cannot create instance: engine not ready");
			return 0;
		}

		uint64_t entityID = static_cast<uint64_t>(static_cast<uint32_t>(entity));
		if (s_CurrentScene && s_CurrentScene->IsValid(entity) && s_CurrentScene->HasComponent<UUIDComponent>(entity)) {
			entityID = static_cast<uint64_t>(s_CurrentScene->GetComponent<UUIDComponent>(entity).Id);
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

} // namespace Bolt
