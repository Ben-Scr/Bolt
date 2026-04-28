#pragma once
#include "Core/Export.hpp"
#include "Scene/EntityHandle.hpp"
#include "Scripting/DotNetHost.hpp"
#include "Scripting/ScriptGlue.hpp"
#include <string>
#include <cstdint>

namespace Bolt {

	class Scene;

	/// <summary>
	/// ScriptEngine manages the CoreCLR (.NET) scripting runtime via hostfxr.
	///
	/// Lifecycle:
	///   1. Init()            — boot CoreCLR runtime
	///   2. LoadCoreAssembly  — load Bolt-ScriptCore.dll, initialize bridge
	///   3. LoadUserAssembly  — load user script DLL (e.g. Bolt-Sandbox.dll)
	///   4. CreateInstance     — per ScriptComponent entry, instantiate managed object
	///   5. InvokeStart/Update/OnDestroy — forward lifecycle calls
	///   6. Shutdown()        — tear down the runtime
	/// </summary>
	class BOLT_API ScriptEngine {
	public:
		static void Init();
		static void Shutdown();
		static bool IsInitialized();

		static void LoadCoreAssembly(const std::string& path);
		static void LoadUserAssembly(const std::string& path);
		static bool HasUserAssembly();
		static void ReloadAssemblies();

		static void SetScene(Scene* scene);
		static Scene* GetScene();

		// ── Instance management ────────────────────────────────────────
		static uint32_t CreateScriptInstance(const std::string& className, EntityHandle entity);
		static void DestroyScriptInstance(uint32_t handle);
		static void InvokeStart(uint32_t handle);
		static void InvokeUpdate(uint32_t handle);
		static void InvokeOnDestroy(uint32_t handle);
		static bool ClassExists(const std::string& className);

		static void RaiseApplicationStart();
		static void RaiseApplicationPaused();
		static void RaiseFocusChanged(bool focused);
		static void RaiseKeyDown(int key);
		static void RaiseKeyUp(int key);
		static void RaiseMouseDown(int button);
		static void RaiseMouseUp(int button);
		static void RaiseMouseScroll(float delta);
		static void RaiseMouseMove(float x, float y);
		static void RaiseBeforeSceneLoaded(const std::string& sceneName);
		static void RaiseSceneLoaded(const std::string& sceneName);
		static void RaiseBeforeSceneUnloaded(const std::string& sceneName);
		static void RaiseSceneUnloaded(const std::string& sceneName);

		static const ManagedCallbacks& GetCallbacks() { return s_Callbacks; }

	private:
		static bool s_Initialized;
		static Scene* s_CurrentScene;
		static std::string s_CoreAssemblyPath;
		static std::string s_UserAssemblyPath;
		static bool s_HasUserAssembly;

		static DotNetHost s_Host;
		static ManagedCallbacks s_Callbacks;
	};

} // namespace Bolt
