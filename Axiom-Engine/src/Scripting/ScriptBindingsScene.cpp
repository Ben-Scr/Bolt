#include "pch.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Core/Application.hpp"
#include "Core/Input.hpp"
#include "Core/Time.hpp"
#include "Core/Window.hpp"
#include "Core/Log.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/File.hpp"
#include "Project/ProjectManager.hpp"
#include "Project/AxiomProject.hpp"
#include "Scripting/ScriptSystem.hpp"
#include <Systems/ParticleUpdateSystem.hpp>
#include <Systems/AudioUpdateSystem.hpp>
#include "Scene/ComponentRegistry.hpp"
#include "Components/General/UUIDComponent.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/General/NameComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/FastBody2DComponent.hpp"
#include "Components/Physics/FastBoxCollider2DComponent.hpp"
#include "Components/Physics/FastCircleCollider2DComponent.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Tags.hpp"
#include "Audio/AudioManager.hpp"
#include "Graphics/TextureManager.hpp"
#include "Graphics/Gizmo.hpp"
#include "Physics/Physics2D.hpp"

namespace Axiom {
	EntityHandle ToEntityHandle(uint64_t id);
	uint64_t FromEntityHandle(EntityHandle handle);
	uint64_t GetEntityScriptId(const Scene& scene, EntityHandle handle);
	bool TryResolveEntityByUUID(const Scene& scene, uint64_t entityID, EntityHandle& outHandle);
	bool ResolveEntityReference(uint64_t entityID, Scene*& outScene, EntityHandle& outHandle);
	Scene* GetScene();
	extern thread_local std::string s_StringReturnBuffer;

	// Thread-local buffer for returning strings to managed code
	// ── Application Bindings ────────────────────────────────────────────

	static float Axiom_Application_GetDeltaTime()
	{
		auto* app = Application::GetInstance();
		return app ? app->GetTime().GetDeltaTime() : 0.0f;
	}

	static float Axiom_Application_GetTime()
	{
		auto* app = Application::GetInstance();
		return app ? app->GetTime().GetElapsedTime() : 0.0f;
	}

	static int Axiom_Application_GetScreenWidth()
	{
		auto* window = Application::GetWindow();
		return window ? window->GetWidth() : 0;
	}

	static int Axiom_Application_GetScreenHeight()
	{
		auto* window = Application::GetWindow();
		return window ? window->GetHeight() : 0;
	}

	static float Axiom_Application_GetTargetFrameRate() { return Application::GetTargetFramerate(); }
	static void  Axiom_Application_SetTargetFrameRate(float fps) { Application::SetTargetFramerate(fps); }

	static void Axiom_Application_Quit() {
		// Only quit in build mode, not in the editor
		if (!Application::GetIsPlaying() || Application::GetInstance() == nullptr) return;
		Application::Quit();
	}

	// Runtime sibling of the compile-time AXIOM_EDITOR define. True when
	// the host process is the editor binary, false in shipped runtime
	// builds. Useful for runtime branches that can't be #if'd because
	// the same script DLL feeds both editor preview and ship builds.
	static int Axiom_Application_IsEditor() {
		return Application::IsEditor() ? 1 : 0;
	}

	static float Axiom_Application_GetFixedDeltaTime() {
		auto* app = Application::GetInstance();
		return app ? app->GetTime().GetFixedDeltaTime() : (1.0f / 50.0f);
	}
	static float Axiom_Application_GetUnscaledDeltaTime() {
		auto* app = Application::GetInstance();
		return app ? app->GetTime().GetDeltaTimeUnscaled() : 0.0f;
	}
	static float Axiom_Application_GetFixedUnscaledDeltaTime() {
		auto* app = Application::GetInstance();
		return app ? app->GetTime().GetUnscaledFixedDeltaTime() : (1.0f / 50.0f);
	}
	static float Axiom_Application_GetTimeScale() {
		auto* app = Application::GetInstance();
		return app ? app->GetTime().GetTimeScale() : 1.0f;
	}
	static void Axiom_Application_SetTimeScale(float scale) {
		auto* app = Application::GetInstance();
		if (app) app->GetTime().SetTimeScale(scale);
	}

	// ── Log Bindings ────────────────────────────────────────────────────

	static void Axiom_Log_Trace(const char* message) { Log::PrintMessageTag(Log::Type::Client, Log::Level::Trace, "Script", message); }
	static void Axiom_Log_Info(const char* message)  { Log::PrintMessageTag(Log::Type::Client, Log::Level::Info, "Script", message); }
	static void Axiom_Log_Warn(const char* message)  { Log::PrintMessageTag(Log::Type::Client, Log::Level::Warn, "Script", message); }
	static void Axiom_Log_Error(const char* message) { Log::PrintMessageTag(Log::Type::Client, Log::Level::Error, "Script", message); }

	// ── Input Bindings ──────────────────────────────────────────────────

	static bool CanReadScriptInput()
	{
		auto* app = Application::GetInstance();
		return app && ScriptBindings::IsScriptInputEnabled();
	}

	static int Axiom_Input_GetKey(int keyCode)
	{
		auto* app = Application::GetInstance();
		return CanReadScriptInput() ? (app->GetInput().GetKey(static_cast<KeyCode>(keyCode)) ? 1 : 0) : 0;
	}

	static int Axiom_Input_GetKeyDown(int keyCode)
	{
		auto* app = Application::GetInstance();
		return CanReadScriptInput() ? (app->GetInput().GetKeyDown(static_cast<KeyCode>(keyCode)) ? 1 : 0) : 0;
	}

	static int Axiom_Input_GetKeyUp(int keyCode)
	{
		auto* app = Application::GetInstance();
		return CanReadScriptInput() ? (app->GetInput().GetKeyUp(static_cast<KeyCode>(keyCode)) ? 1 : 0) : 0;
	}

	static int Axiom_Input_GetMouseButton(int button)
	{
		auto* app = Application::GetInstance();
		return CanReadScriptInput() ? (app->GetInput().GetMouse(static_cast<MouseButton>(button)) ? 1 : 0) : 0;
	}

	static int Axiom_Input_GetMouseButtonDown(int button)
	{
		auto* app = Application::GetInstance();
		return CanReadScriptInput() ? (app->GetInput().GetMouseDown(static_cast<MouseButton>(button)) ? 1 : 0) : 0;
	}

	static int Axiom_Input_GetMouseButtonUp(int button)
	{
		auto* app = Application::GetInstance();
		return CanReadScriptInput() ? (app->GetInput().GetMouseUp(static_cast<MouseButton>(button)) ? 1 : 0) : 0;
	}

	static void Axiom_Input_GetMousePosition(float* outX, float* outY)
	{
		auto* app = Application::GetInstance();
		if (CanReadScriptInput()) { Vec2 pos = app->GetInput().GetMousePosition(); *outX = pos.x; *outY = pos.y; }
		else { *outX = 0.0f; *outY = 0.0f; }
	}

	static void Axiom_Input_GetAxis(float* outX, float* outY) {
		auto* app = Application::GetInstance();
		if (CanReadScriptInput()) { Vec2 axis = app->GetInput().GetAxis(); *outX = axis.x; *outY = axis.y; }
		else { *outX = 0.0f; *outY = 0.0f; }
	}
	static void Axiom_Input_GetMouseDelta(float* outX, float* outY) {
		auto* app = Application::GetInstance();
		if (CanReadScriptInput()) { Vec2 delta = app->GetInput().GetMouseDelta(); *outX = delta.x; *outY = delta.y; }
		else { *outX = 0.0f; *outY = 0.0f; }
	}
	static float Axiom_Input_GetScrollWheelDelta() {
		auto* app = Application::GetInstance();
		return CanReadScriptInput() ? app->GetInput().ScrollValue() : 0.0f;
	}

	// ── Entity Bindings ─────────────────────────────────────────────────

	static int Axiom_Entity_IsValid(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		return ResolveEntityReference(entityID, scene, handle) ? 1 : 0;
	}

	static uint64_t Axiom_Entity_FindByName(const char* name)
	{
		Scene* scene = GetScene();
		if (!scene || !name) return 0;
		std::string targetName(name);
		auto& registry = scene->GetRegistry();
		auto view = registry.view<NameComponent>();
		for (auto [entity, nameComp] : view.each())
			if (nameComp.Name == targetName) return GetEntityScriptId(*scene, entity);
		return 0;
	}

	static void Axiom_Entity_Destroy(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return;
		scene->DestroyEntity(handle);
	}

	static uint64_t Axiom_Entity_Create(const char* name)
	{
		Scene* scene = GetScene();
		if (!scene) return 0;
		Entity entity = scene->CreateRuntimeEntity(name ? name : "Entity");
		return GetEntityScriptId(*scene, entity.GetHandle());
	}

	void PopulateNonComponentBindings(NativeBindings& b)
	{
		b.Application_GetDeltaTime = &Axiom_Application_GetDeltaTime;
		b.Application_GetElapsedTime = &Axiom_Application_GetTime;
		b.Application_GetScreenWidth = &Axiom_Application_GetScreenWidth;
		b.Application_GetScreenHeight = &Axiom_Application_GetScreenHeight;
		b.Application_IsEditor = &Axiom_Application_IsEditor;
		b.Application_GetTargetFrameRate = &Axiom_Application_GetTargetFrameRate;
		b.Application_SetTargetFrameRate = &Axiom_Application_SetTargetFrameRate;
		b.Application_Quit = &Axiom_Application_Quit;
		b.Application_GetFixedDeltaTime = &Axiom_Application_GetFixedDeltaTime;
		b.Application_GetUnscaledDeltaTime = &Axiom_Application_GetUnscaledDeltaTime;
		b.Application_GetFixedUnscaledDeltaTime = &Axiom_Application_GetFixedUnscaledDeltaTime;
		b.Application_GetTimeScale = &Axiom_Application_GetTimeScale;
		b.Application_SetTimeScale = &Axiom_Application_SetTimeScale;

		b.Log_Trace = &Axiom_Log_Trace;
		b.Log_Info = &Axiom_Log_Info;
		b.Log_Warn = &Axiom_Log_Warn;
		b.Log_Error = &Axiom_Log_Error;

		b.Input_GetKey = &Axiom_Input_GetKey;
		b.Input_GetKeyDown = &Axiom_Input_GetKeyDown;
		b.Input_GetKeyUp = &Axiom_Input_GetKeyUp;
		b.Input_GetMouseButton = &Axiom_Input_GetMouseButton;
		b.Input_GetMouseButtonDown = &Axiom_Input_GetMouseButtonDown;
		b.Input_GetMouseButtonUp = &Axiom_Input_GetMouseButtonUp;
		b.Input_GetMousePosition = &Axiom_Input_GetMousePosition;
		b.Input_GetAxis = &Axiom_Input_GetAxis;
		b.Input_GetMouseDelta = &Axiom_Input_GetMouseDelta;
		b.Input_GetScrollWheelDelta = &Axiom_Input_GetScrollWheelDelta;

		b.Entity_IsValid = &Axiom_Entity_IsValid;
		b.Entity_FindByName = &Axiom_Entity_FindByName;
		b.Entity_Destroy = &Axiom_Entity_Destroy;
		b.Entity_Create = &Axiom_Entity_Create;
	}

} // namespace Axiom
