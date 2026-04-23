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
#include "Project/BoltProject.hpp"
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
#include "Components/Physics/BoltBody2DComponent.hpp"
#include "Components/Physics/BoltBoxCollider2DComponent.hpp"
#include "Components/Physics/BoltCircleCollider2DComponent.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Tags.hpp"
#include "Audio/AudioManager.hpp"
#include "Graphics/TextureManager.hpp"
#include "Graphics/Gizmo.hpp"
#include "Physics/Physics2D.hpp"

namespace Bolt {
	EntityHandle ToEntityHandle(uint64_t id);
	uint64_t FromEntityHandle(EntityHandle handle);
	uint64_t GetEntityScriptId(const Scene& scene, EntityHandle handle);
	bool TryResolveEntityByUUID(const Scene& scene, uint64_t entityID, EntityHandle& outHandle);
	bool ResolveEntityReference(uint64_t entityID, Scene*& outScene, EntityHandle& outHandle);
	Scene* GetScene();
	extern thread_local std::string s_StringReturnBuffer;

	// Thread-local buffer for returning strings to managed code
	// ── Application Bindings ────────────────────────────────────────────

	static float Bolt_Application_GetDeltaTime()
	{
		auto* app = Application::GetInstance();
		return app ? app->GetTime().GetDeltaTime() : 0.0f;
	}

	static float Bolt_Application_GetTime()
	{
		auto* app = Application::GetInstance();
		return app ? app->GetTime().GetElapsedTime() : 0.0f;
	}

	static int Bolt_Application_GetScreenWidth()
	{
		auto* window = Application::GetWindow();
		return window ? window->GetWidth() : 0;
	}

	static int Bolt_Application_GetScreenHeight()
	{
		auto* window = Application::GetWindow();
		return window ? window->GetHeight() : 0;
	}

	static float Bolt_Application_GetTargetFrameRate() { return Application::GetTargetFramerate(); }
	static void  Bolt_Application_SetTargetFrameRate(float fps) { Application::SetTargetFramerate(fps); }

	static void Bolt_Application_Quit() {
		// Only quit in build mode, not in the editor
		if (!Application::GetIsPlaying() || Application::GetInstance() == nullptr) return;
		Application::Quit();
	}

	static float Bolt_Application_GetFixedDeltaTime() {
		auto* app = Application::GetInstance();
		return app ? app->GetTime().GetFixedDeltaTime() : (1.0f / 50.0f);
	}
	static float Bolt_Application_GetUnscaledDeltaTime() {
		auto* app = Application::GetInstance();
		return app ? app->GetTime().GetDeltaTimeUnscaled() : 0.0f;
	}
	static float Bolt_Application_GetFixedUnscaledDeltaTime() {
		auto* app = Application::GetInstance();
		return app ? app->GetTime().GetUnscaledFixedDeltaTime() : (1.0f / 50.0f);
	}

	// ── Log Bindings ────────────────────────────────────────────────────

	static void Bolt_Log_Trace(const char* message) { Log::PrintMessageTag(Log::Type::Client, Log::Level::Trace, "Script", message); }
	static void Bolt_Log_Info(const char* message)  { Log::PrintMessageTag(Log::Type::Client, Log::Level::Info, "Script", message); }
	static void Bolt_Log_Warn(const char* message)  { Log::PrintMessageTag(Log::Type::Client, Log::Level::Warn, "Script", message); }
	static void Bolt_Log_Error(const char* message) { Log::PrintMessageTag(Log::Type::Client, Log::Level::Error, "Script", message); }

	// ── Input Bindings ──────────────────────────────────────────────────

	static bool CanReadScriptInput()
	{
		auto* app = Application::GetInstance();
		return app && Application::IsGameInputEnabled();
	}

	static int Bolt_Input_GetKey(int keyCode)
	{
		auto* app = Application::GetInstance();
		return CanReadScriptInput() ? (app->GetInput().GetKey(static_cast<KeyCode>(keyCode)) ? 1 : 0) : 0;
	}

	static int Bolt_Input_GetKeyDown(int keyCode)
	{
		auto* app = Application::GetInstance();
		return CanReadScriptInput() ? (app->GetInput().GetKeyDown(static_cast<KeyCode>(keyCode)) ? 1 : 0) : 0;
	}

	static int Bolt_Input_GetKeyUp(int keyCode)
	{
		auto* app = Application::GetInstance();
		return CanReadScriptInput() ? (app->GetInput().GetKeyUp(static_cast<KeyCode>(keyCode)) ? 1 : 0) : 0;
	}

	static int Bolt_Input_GetMouseButton(int button)
	{
		auto* app = Application::GetInstance();
		return CanReadScriptInput() ? (app->GetInput().GetMouse(static_cast<MouseButton>(button)) ? 1 : 0) : 0;
	}

	static int Bolt_Input_GetMouseButtonDown(int button)
	{
		auto* app = Application::GetInstance();
		return CanReadScriptInput() ? (app->GetInput().GetMouseDown(static_cast<MouseButton>(button)) ? 1 : 0) : 0;
	}

	static void Bolt_Input_GetMousePosition(float* outX, float* outY)
	{
		auto* app = Application::GetInstance();
		if (CanReadScriptInput()) { Vec2 pos = app->GetInput().GetMousePosition(); *outX = pos.x; *outY = pos.y; }
		else { *outX = 0.0f; *outY = 0.0f; }
	}

	static void Bolt_Input_GetAxis(float* outX, float* outY) {
		auto* app = Application::GetInstance();
		if (CanReadScriptInput()) { Vec2 axis = app->GetInput().GetAxis(); *outX = axis.x; *outY = axis.y; }
		else { *outX = 0.0f; *outY = 0.0f; }
	}
	static void Bolt_Input_GetMouseDelta(float* outX, float* outY) {
		auto* app = Application::GetInstance();
		if (CanReadScriptInput()) { Vec2 delta = app->GetInput().GetMouseDelta(); *outX = delta.x; *outY = delta.y; }
		else { *outX = 0.0f; *outY = 0.0f; }
	}
	static float Bolt_Input_GetScrollWheelDelta() {
		auto* app = Application::GetInstance();
		return CanReadScriptInput() ? app->GetInput().ScrollValue() : 0.0f;
	}

	// ── Entity Bindings ─────────────────────────────────────────────────

	static int Bolt_Entity_IsValid(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		return ResolveEntityReference(entityID, scene, handle) ? 1 : 0;
	}

	static uint64_t Bolt_Entity_FindByName(const char* name)
	{
		Scene* scene = GetScene();
		if (!scene) return 0;
		std::string targetName(name);
		auto& registry = scene->GetRegistry();
		auto view = registry.view<NameComponent>();
		for (auto [entity, nameComp] : view.each())
			if (nameComp.Name == targetName) return GetEntityScriptId(*scene, entity);
		return 0;
	}

	static void Bolt_Entity_Destroy(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return;
		scene->DestroyEntity(handle);
	}

	static uint64_t Bolt_Entity_Create(const char* name)
	{
		Scene* scene = GetScene();
		if (!scene) return 0;
		Entity entity = scene->CreateEntity(name ? name : "Entity");
		return GetEntityScriptId(*scene, entity.GetHandle());
	}

	void PopulateNonComponentBindings(NativeBindings& b)
	{
		b.Application_GetDeltaTime = &Bolt_Application_GetDeltaTime;
		b.Application_GetElapsedTime = &Bolt_Application_GetTime;
		b.Application_GetScreenWidth = &Bolt_Application_GetScreenWidth;
		b.Application_GetScreenHeight = &Bolt_Application_GetScreenHeight;
		b.Application_GetTargetFrameRate = &Bolt_Application_GetTargetFrameRate;
		b.Application_SetTargetFrameRate = &Bolt_Application_SetTargetFrameRate;
		b.Application_Quit = &Bolt_Application_Quit;
		b.Application_GetFixedDeltaTime = &Bolt_Application_GetFixedDeltaTime;
		b.Application_GetUnscaledDeltaTime = &Bolt_Application_GetUnscaledDeltaTime;
		b.Application_GetFixedUnscaledDeltaTime = &Bolt_Application_GetFixedUnscaledDeltaTime;

		b.Log_Trace = &Bolt_Log_Trace;
		b.Log_Info = &Bolt_Log_Info;
		b.Log_Warn = &Bolt_Log_Warn;
		b.Log_Error = &Bolt_Log_Error;

		b.Input_GetKey = &Bolt_Input_GetKey;
		b.Input_GetKeyDown = &Bolt_Input_GetKeyDown;
		b.Input_GetKeyUp = &Bolt_Input_GetKeyUp;
		b.Input_GetMouseButton = &Bolt_Input_GetMouseButton;
		b.Input_GetMouseButtonDown = &Bolt_Input_GetMouseButtonDown;
		b.Input_GetMousePosition = &Bolt_Input_GetMousePosition;
		b.Input_GetAxis = &Bolt_Input_GetAxis;
		b.Input_GetMouseDelta = &Bolt_Input_GetMouseDelta;
		b.Input_GetScrollWheelDelta = &Bolt_Input_GetScrollWheelDelta;

		b.Entity_IsValid = &Bolt_Entity_IsValid;
		b.Entity_FindByName = &Bolt_Entity_FindByName;
		b.Entity_Destroy = &Bolt_Entity_Destroy;
		b.Entity_Create = &Bolt_Entity_Create;
	}

} // namespace Bolt
