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
	EntityHandle ToEntityHandle(uint64_t id)
	{
		return static_cast<EntityHandle>(static_cast<uint32_t>(id));
	}

	uint64_t FromEntityHandle(EntityHandle handle)
	{
		return static_cast<uint64_t>(static_cast<uint32_t>(handle));
	}

	uint64_t GetEntityScriptId(const Scene& scene, EntityHandle handle)
	{
		if (scene.IsValid(handle) && scene.HasComponent<UUIDComponent>(handle)) {
			return static_cast<uint64_t>(scene.GetComponent<UUIDComponent>(handle).Id);
		}

		return FromEntityHandle(handle);
	}

	bool TryResolveEntityByUUID(const Scene& scene, uint64_t entityID, EntityHandle& outHandle)
	{
		auto view = scene.GetRegistry().view<UUIDComponent>();
		for (EntityHandle handle : view) {
			if (static_cast<uint64_t>(view.get<UUIDComponent>(handle).Id) == entityID) {
				outHandle = handle;
				return true;
			}
		}

		return false;
	}

	bool ResolveEntityReference(uint64_t entityID, Scene*& outScene, EntityHandle& outHandle)
	{
		outScene = nullptr;
		outHandle = entt::null;

		if (entityID == 0) {
			return false;
		}

		Scene* currentScene = ScriptEngine::GetScene();
		if (currentScene) {
			if (TryResolveEntityByUUID(*currentScene, entityID, outHandle)) {
				outScene = currentScene;
				return true;
			}

			const EntityHandle rawHandle = ToEntityHandle(entityID);
			if (currentScene->IsValid(rawHandle)) {
				outScene = currentScene;
				outHandle = rawHandle;
				return true;
			}
		}

		SceneManager::Get().ForeachLoadedScene([&](const Scene& scene) {
			if (outScene || &scene == currentScene) {
				return;
			}

			EntityHandle resolvedHandle = entt::null;
			if (TryResolveEntityByUUID(scene, entityID, resolvedHandle)) {
				outScene = const_cast<Scene*>(&scene);
				outHandle = resolvedHandle;
			}
		});

		return outScene != nullptr;
	}

	Scene* GetScene()
	{
		Scene* scene = ScriptEngine::GetScene();
		if (!scene)
		{
			BT_CORE_ERROR_TAG("ScriptBindings", "No active scene set on ScriptEngine");
		}
		return scene;
	}

	thread_local std::string s_StringReturnBuffer;

	void PopulateNonComponentBindings(NativeBindings& b);

	#define GET_COMPONENT(Type, entityID, failReturn) \
		Scene* scene = nullptr; \
		EntityHandle handle = entt::null; \
		if (!ResolveEntityReference(entityID, scene, handle)) return failReturn; \
		if (!scene->HasComponent<Type>(handle)) return failReturn; \
		auto& comp = scene->GetComponent<Type>(handle)


	// Helper: find ComponentInfo by display name
	static const ComponentInfo* FindComponentByName(const std::string& name) {
		const auto& registry = SceneManager::Get().GetComponentRegistry();
		const ComponentInfo* found = nullptr;
		registry.ForEachComponentInfo([&](const std::type_index&, const ComponentInfo& info) {
			if (info.displayName == name)
				found = &info;
		});
		return found;
	}

	int Bolt_Entity_HasComponent(uint64_t entityID, const char* componentName)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;

		const ComponentInfo* info = FindComponentByName(componentName);
		if (!info || !info->has) return 0;
		return info->has(scene->GetEntity(handle)) ? 1 : 0;
	}

	int Bolt_Entity_AddComponent(uint64_t entityID, const char* componentName)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;

		const ComponentInfo* info = FindComponentByName(componentName);
		if (!info || !info->add) return 0;

		Entity entity = scene->GetEntity(handle);
		if (info->has && info->has(entity)) return 1;
		info->add(entity);
		return 1;
	}

	int Bolt_Entity_RemoveComponent(uint64_t entityID, const char* componentName)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;

		const ComponentInfo* info = FindComponentByName(componentName);
		if (!info || !info->remove) return 0;

		Entity entity = scene->GetEntity(handle);
		if (info->has && !info->has(entity)) return 0;
		info->remove(entity);
		return 1;
	}

	uint64_t Bolt_Entity_Clone(uint64_t sourceEntityID)
	{
		Scene* targetScene = GetScene();
		if (!targetScene) return 0;

		Scene* sourceScene = nullptr;
		EntityHandle sourceHandle = entt::null;
		if (!ResolveEntityReference(sourceEntityID, sourceScene, sourceHandle)) return 0;

		Entity source = sourceScene->GetEntity(sourceHandle);
		std::string name = source.GetName();

		Entity clone = targetScene->CreateEntity(name + " (Clone)");

		const auto& registry = SceneManager::Get().GetComponentRegistry();
		registry.ForEachComponentInfo([&](const std::type_index&, const ComponentInfo& info) {
			if (info.category != ComponentCategory::Component) return;
			if (!info.has(source)) return;
			if (info.copyTo) {
				info.copyTo(source, clone);
			}
		});

		return GetEntityScriptId(*targetScene, clone.GetHandle());
	}

	// ── Scene ───────────────────────────────────────────────────────────

	static const char* Bolt_Scene_GetActiveSceneName() {
		Scene* scene = GetScene();
		if (!scene) { s_StringReturnBuffer.clear(); return s_StringReturnBuffer.c_str(); }
		s_StringReturnBuffer = scene->GetName();
		return s_StringReturnBuffer.c_str();
	}

	static int CountSceneEntities(const Scene& scene) {
		int count = 0;
		auto view = scene.GetRegistry().view<entt::entity>();
		for (EntityHandle entityHandle : view) {
			if (scene.GetRegistry().valid(entityHandle)) {
				++count;
			}
		}

		return count;
	}

	static int Bolt_Scene_GetEntityCount() {
		Scene* scene = GetScene();
		if (!scene) return 0;
		return CountSceneEntities(*scene);
	}

	static const char* Bolt_Scene_GetEntityNameByUUID(uint64_t uuid) {
		Scene* scene = GetScene();
		if (!scene) { s_StringReturnBuffer.clear(); return s_StringReturnBuffer.c_str(); }
		auto view = scene->GetRegistry().view<UUIDComponent, NameComponent>();
		for (auto [ent, uuidComp, nameComp] : view.each()) {
			if (static_cast<uint64_t>(uuidComp.Id) == uuid) {
				s_StringReturnBuffer = nameComp.Name;
				return s_StringReturnBuffer.c_str();
			}
		}
		s_StringReturnBuffer.clear();
		return s_StringReturnBuffer.c_str();
	}

	static int LoadSceneByName(const char* sceneName, bool additive) {
		auto& sm = SceneManager::Get();
		std::string name(sceneName);
		BoltProject* project = ProjectManager::GetCurrentProject();
		const bool hasDefinition = sm.HasSceneDefinition(name);

		if (!hasDefinition) {
			auto& definition = sm.RegisterScene(name);
			if (project) {
				const std::string scenePath = project->GetSceneFilePath(name);
				definition.OnLoad([scenePath](Scene& scene) {
					if (File::Exists(scenePath)) {
						SceneSerializer::LoadFromFile(scene, scenePath);
					}
				});
			}
		}

		auto sceneWeak = additive ? sm.LoadSceneAdditive(name) : sm.LoadScene(name);
		auto scenePtr = sceneWeak.lock();
		if (!scenePtr) {
			BT_CORE_ERROR_TAG("ScriptBindings", "Failed to load scene{}: {}", additive ? " additively" : "", name);
			return 0;
		}

		BT_INFO_TAG("ScriptBindings", "Loaded scene{}: {}", additive ? " additively" : "", name);
		return 1;
	}

	static int Bolt_Scene_LoadAdditive(const char* sceneName) {
		return LoadSceneByName(sceneName, true);
	}

	static int Bolt_Scene_Load(const char* sceneName) {
		return LoadSceneByName(sceneName, false);
	}

	static void Bolt_Scene_Unload(const char* sceneName) {
		SceneManager::Get().UnloadScene(sceneName);
	}

	static int Bolt_Scene_SetActive(const char* sceneName) {
		return SceneManager::Get().SetActiveScene(sceneName) ? 1 : 0;
	}

	static int Bolt_Scene_Reload(const char* sceneName) {
		auto result = SceneManager::Get().ReloadScene(sceneName);
		return result.lock() ? 1 : 0;
	}

	static int Bolt_Scene_GetLoadedCount() {
		return static_cast<int>(SceneManager::Get().GetLoadedScenes().size());
	}

	static const char* Bolt_Scene_GetLoadedSceneNameAt(int index) {
		auto scenes = SceneManager::Get().GetLoadedScenes();
		if (index < 0 || index >= static_cast<int>(scenes.size())) {
			s_StringReturnBuffer.clear();
			return s_StringReturnBuffer.c_str();
		}
		auto scene = scenes[index].lock();
		s_StringReturnBuffer = scene ? scene->GetName() : "";
		return s_StringReturnBuffer.c_str();
	}

	// ── Scene Query ─────────────────────────────────────────────────────

	static int Bolt_Scene_QueryEntities(const char* componentNames, uint64_t* outEntityIDs, int maxOut) {
		Scene* scene = GetScene();
		if (!scene || !componentNames || !outEntityIDs || maxOut <= 0) return 0;

		// Parse pipe-delimited component names
		std::vector<const ComponentInfo*> infos;
		std::string names(componentNames);
		size_t start = 0;
		while (start < names.size()) {
			size_t end = names.find('|', start);
			if (end == std::string::npos) end = names.size();
			std::string name = names.substr(start, end - start);
			const ComponentInfo* info = FindComponentByName(name);
			if (!info || !info->has) return 0; // Unknown component = 0 results
			infos.push_back(info);
			start = end + 1;
		}
		if (infos.empty()) return 0;

		int count = 0;
		auto& registry = scene->GetRegistry();
		auto view = registry.view<entt::entity>();
		for (EntityHandle entityHandle : view) {
			if (!registry.valid(entityHandle)) continue;

			Entity entity = scene->GetEntity(entityHandle);
			bool match = true;
			for (auto* info : infos) {
				if (!info->has(entity)) { match = false; break; }
			}
			if (match) {
				if (count < maxOut)
					outEntityIDs[count] = GetEntityScriptId(*scene, entityHandle);
				count++;
			}
		}
		return count;
	}

	static int Bolt_Asset_IsValid(uint64_t assetId)
	{
		return AssetRegistry::Exists(assetId) ? 1 : 0;
	}

	static uint64_t Bolt_Asset_GetOrCreateUUIDFromPath(const char* path)
	{
		if (!path || path[0] == '\0') {
			return 0;
		}

		return AssetRegistry::GetOrCreateAssetUUID(path);
	}

	static const char* Bolt_Asset_GetPath(uint64_t assetId)
	{
		s_StringReturnBuffer = AssetRegistry::ResolvePath(assetId);
		return s_StringReturnBuffer.c_str();
	}

	static const char* Bolt_Asset_GetDisplayName(uint64_t assetId)
	{
		s_StringReturnBuffer = AssetRegistry::GetDisplayName(assetId);
		return s_StringReturnBuffer.c_str();
	}

	static int Bolt_Texture_LoadAsset(uint64_t assetId)
	{
		return TextureManager::LoadTextureByUUID(assetId).IsValid() ? 1 : 0;
	}

	static int Bolt_Texture_GetWidth(uint64_t assetId)
	{
		TextureHandle handle = TextureManager::LoadTextureByUUID(assetId);
		Texture2D* texture = TextureManager::GetTexture(handle);
		return texture ? static_cast<int>(texture->GetWidth()) : 0;
	}

	static int Bolt_Texture_GetHeight(uint64_t assetId)
	{
		TextureHandle handle = TextureManager::LoadTextureByUUID(assetId);
		Texture2D* texture = TextureManager::GetTexture(handle);
		return texture ? static_cast<int>(texture->GetHeight()) : 0;
	}

	static int Bolt_Audio_LoadAsset(uint64_t assetId)
	{
		return AudioManager::LoadAudioByUUID(assetId).IsValid() ? 1 : 0;
	}

	static void Bolt_Audio_PlayOneShotAsset(uint64_t assetId, float volume)
	{
		AudioHandle handle = AudioManager::LoadAudioByUUID(assetId);
		if (handle.IsValid()) {
			AudioManager::PlayOneShot(handle, volume);
		}
	}

	// Helper: parse pipe-delimited names into ComponentInfo pointers
	static bool ParseComponentNames(const char* names, std::vector<const ComponentInfo*>& out) {
		if (!names || names[0] == '\0') return true; // empty is valid (no filter)
		std::string str(names);
		size_t start = 0;
		while (start < str.size()) {
			size_t end = str.find('|', start);
			if (end == std::string::npos) end = str.size();
			std::string name = str.substr(start, end - start);
			if (!name.empty()) {
				const ComponentInfo* info = FindComponentByName(name);
				if (!info || !info->has) return false;
				out.push_back(info);
			}
			start = end + 1;
		}
		return true;
	}

	static int Bolt_Scene_QueryEntitiesFiltered(
		const char* withComponents,
		const char* withoutComponents,
		const char* mustHaveComponents,
		int enableFilter,
		uint64_t* outEntityIDs, int maxOut)
	{
		Scene* scene = GetScene();
		if (!scene || !outEntityIDs || maxOut <= 0) return 0;

		std::vector<const ComponentInfo*> withInfos, withoutInfos, mustHaveInfos;
		if (!ParseComponentNames(withComponents, withInfos)) return 0;
		if (!ParseComponentNames(withoutComponents, withoutInfos)) return 0;
		if (!ParseComponentNames(mustHaveComponents, mustHaveInfos)) return 0;
		if (withInfos.empty() && mustHaveInfos.empty()) return 0;

		int count = 0;
		auto& registry = scene->GetRegistry();
		auto view = registry.view<entt::entity>();

		for (auto entityHandle : view) {
			if (!registry.valid(entityHandle)) continue;

			Entity entity = scene->GetEntity(entityHandle);

			// Enable filter: 1 = enabled only, 2 = disabled only
			if (enableFilter == 1 && registry.all_of<DisabledTag>(entityHandle)) continue;
			if (enableFilter == 2 && !registry.all_of<DisabledTag>(entityHandle)) continue;

			// WITH: entity must have all these components
			bool match = true;
			for (auto* info : withInfos) {
				if (!info->has(entity)) { match = false; break; }
			}
			if (!match) continue;

			// MUST HAVE (With<>): entity must have these too
			for (auto* info : mustHaveInfos) {
				if (!info->has(entity)) { match = false; break; }
			}
			if (!match) continue;

			// WITHOUT: entity must NOT have any of these
			for (auto* info : withoutInfos) {
				if (info->has(entity)) { match = false; break; }
			}
			if (!match) continue;

			if (count < maxOut)
				outEntityIDs[count] = GetEntityScriptId(*scene, entityHandle);
			count++;
		}
		return count;
	}

	// ── NameComponent ───────────────────────────────────────────────────

	static const char* Bolt_NameComponent_GetName(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle) || !scene->HasComponent<NameComponent>(handle)) {
			s_StringReturnBuffer.clear();
			return s_StringReturnBuffer.c_str();
		}

		s_StringReturnBuffer = scene->GetComponent<NameComponent>(handle).Name;
		return s_StringReturnBuffer.c_str();
	}

	static void Bolt_NameComponent_SetName(uint64_t entityID, const char* name)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle) || !scene->HasComponent<NameComponent>(handle)) return;
		scene->GetComponent<NameComponent>(handle).Name = name;
	}

	// ── Transform2D ─────────────────────────────────────────────────────

	static void Bolt_Transform2D_GetPosition(uint64_t entityID, float* outX, float* outY)
	{
		GET_COMPONENT(Transform2DComponent, entityID, (void)(*outX = 0, *outY = 0));
		*outX = comp.Position.x; *outY = comp.Position.y;
	}

	static void Bolt_Transform2D_SetPosition(uint64_t entityID, float x, float y)
	{
		GET_COMPONENT(Transform2DComponent, entityID, );
		comp.Position = { x, y };
	}

	static float Bolt_Transform2D_GetRotation(uint64_t entityID)
	{
		GET_COMPONENT(Transform2DComponent, entityID, 0.0f);
		return comp.Rotation;
	}

	static void Bolt_Transform2D_SetRotation(uint64_t entityID, float rotation)
	{
		GET_COMPONENT(Transform2DComponent, entityID, );
		comp.Rotation = rotation;
	}

	static void Bolt_Transform2D_GetScale(uint64_t entityID, float* outX, float* outY)
	{
		GET_COMPONENT(Transform2DComponent, entityID, (void)(*outX = 1, *outY = 1));
		*outX = comp.Scale.x; *outY = comp.Scale.y;
	}

	static void Bolt_Transform2D_SetScale(uint64_t entityID, float x, float y)
	{
		GET_COMPONENT(Transform2DComponent, entityID, );
		comp.Scale = { x, y };
	}

	// ── SpriteRenderer ──────────────────────────────────────────────────

	static void Bolt_SpriteRenderer_GetColor(uint64_t entityID, float* r, float* g, float* b, float* a)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, (void)(*r = 1, *g = 1, *b = 1, *a = 1));
		*r = comp.Color.r; *g = comp.Color.g; *b = comp.Color.b; *a = comp.Color.a;
	}

	static void Bolt_SpriteRenderer_SetColor(uint64_t entityID, float r, float g, float b, float a)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, );
		comp.Color = { r, g, b, a };
	}

	static uint64_t Bolt_SpriteRenderer_GetTexture(uint64_t entityID)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, 0);

		uint64_t assetId = static_cast<uint64_t>(comp.TextureAssetId);
		if (assetId == 0 && TextureManager::IsValid(comp.TextureHandle)) {
			assetId = TextureManager::GetTextureAssetUUID(comp.TextureHandle);
			if (assetId != 0) {
				comp.TextureAssetId = UUID(assetId);
			}
		}

		return assetId;
	}

	static void Bolt_SpriteRenderer_SetTexture(uint64_t entityID, uint64_t assetId)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, );

		if (assetId == 0) {
			comp.TextureHandle = TextureHandle::Invalid();
			comp.TextureAssetId = UUID(0);
			return;
		}

		comp.TextureAssetId = UUID(assetId);
		comp.TextureHandle = TextureManager::LoadTextureByUUID(assetId);
	}

	static int Bolt_SpriteRenderer_GetSortingOrder(uint64_t entityID)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, 0);
		return comp.SortingOrder;
	}

	static void Bolt_SpriteRenderer_SetSortingOrder(uint64_t entityID, int order)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, );
		comp.SortingOrder = static_cast<short>(order);
	}

	static int Bolt_SpriteRenderer_GetSortingLayer(uint64_t entityID)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, 0);
		return comp.SortingLayer;
	}

	static void Bolt_SpriteRenderer_SetSortingLayer(uint64_t entityID, int layer)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, );
		comp.SortingLayer = static_cast<uint8_t>(layer);
	}

	// ── Camera2D ────────────────────────────────────────────────────────

	static float Bolt_Camera2D_GetOrthographicSize(uint64_t entityID)
	{
		GET_COMPONENT(Camera2DComponent, entityID, 5.0f);
		return comp.GetOrthographicSize();
	}

	static void Bolt_Camera2D_SetOrthographicSize(uint64_t entityID, float size)
	{
		GET_COMPONENT(Camera2DComponent, entityID, );
		comp.SetOrthographicSize(size);
	}

	static float Bolt_Camera2D_GetZoom(uint64_t entityID)
	{
		GET_COMPONENT(Camera2DComponent, entityID, 1.0f);
		return comp.GetZoom();
	}

	static void Bolt_Camera2D_SetZoom(uint64_t entityID, float zoom)
	{
		GET_COMPONENT(Camera2DComponent, entityID, );
		comp.SetZoom(zoom);
	}

	static void Bolt_Camera2D_GetClearColor(uint64_t entityID, float* r, float* g, float* b, float* a)
	{
		GET_COMPONENT(Camera2DComponent, entityID, (void)(*r = 0.1f, *g = 0.1f, *b = 0.1f, *a = 1.0f));
		const auto& cc = comp.GetClearColor();
		*r = cc.r; *g = cc.g; *b = cc.b; *a = cc.a;
	}

	static void Bolt_Camera2D_SetClearColor(uint64_t entityID, float r, float g, float b, float a)
	{
		GET_COMPONENT(Camera2DComponent, entityID, );
		comp.SetClearColor(Color(r, g, b, a));
	}

	static void Bolt_Camera2D_ScreenToWorld(uint64_t entityID, float sx, float sy, float* outX, float* outY)
	{
		GET_COMPONENT(Camera2DComponent, entityID, (void)(*outX = 0, *outY = 0));
		Vec2 world = comp.ScreenToWorld({ sx, sy });
		*outX = world.x; *outY = world.y;
	}

	static float Bolt_Camera2D_GetViewportWidth(uint64_t entityID)
	{
		GET_COMPONENT(Camera2DComponent, entityID, 0.0f);
		return comp.ViewportWidth();
	}

	static float Bolt_Camera2D_GetViewportHeight(uint64_t entityID)
	{
		GET_COMPONENT(Camera2DComponent, entityID, 0.0f);
		return comp.ViewportHeight();
	}

	// ── Rigidbody2D ─────────────────────────────────────────────────────

	static void Bolt_Rigidbody2D_ApplyForce(uint64_t entityID, float forceX, float forceY, int wake)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		b2BodyId bodyId = comp.GetBodyHandle();
		if (b2Body_IsValid(bodyId)) b2Body_ApplyForceToCenter(bodyId, { forceX, forceY }, wake != 0);
	}

	static void Bolt_Rigidbody2D_ApplyImpulse(uint64_t entityID, float impulseX, float impulseY, int wake)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		b2BodyId bodyId = comp.GetBodyHandle();
		if (b2Body_IsValid(bodyId)) b2Body_ApplyLinearImpulseToCenter(bodyId, { impulseX, impulseY }, wake != 0);
	}

	static void Bolt_Rigidbody2D_GetLinearVelocity(uint64_t entityID, float* outX, float* outY)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, (void)(*outX = 0, *outY = 0));
		Vec2 vel = comp.GetVelocity(); *outX = vel.x; *outY = vel.y;
	}

	static void Bolt_Rigidbody2D_SetLinearVelocity(uint64_t entityID, float x, float y)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		comp.SetVelocity({ x, y });
	}

	static float Bolt_Rigidbody2D_GetAngularVelocity(uint64_t entityID)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, 0.0f);
		return comp.GetAngularVelocity();
	}

	static void Bolt_Rigidbody2D_SetAngularVelocity(uint64_t entityID, float velocity)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		comp.SetAngularVelocity(velocity);
	}

	static int Bolt_Rigidbody2D_GetBodyType(uint64_t entityID)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, 0);
		return static_cast<int>(comp.GetBodyType());
	}

	static void Bolt_Rigidbody2D_SetBodyType(uint64_t entityID, int type)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		comp.SetBodyType(static_cast<BodyType>(type));
	}

	static float Bolt_Rigidbody2D_GetGravityScale(uint64_t entityID)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, 1.0f);
		return comp.GetGravityScale();
	}

	static void Bolt_Rigidbody2D_SetGravityScale(uint64_t entityID, float scale)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		comp.SetGravityScale(scale);
	}

	static float Bolt_Rigidbody2D_GetMass(uint64_t entityID)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, 1.0f);
		return comp.GetMass();
	}

	static void Bolt_Rigidbody2D_SetMass(uint64_t entityID, float mass)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		comp.SetMass(mass);
	}

	// ── BoxCollider2D ───────────────────────────────────────────────────

	static void Bolt_BoxCollider2D_GetScale(uint64_t entityID, float* outX, float* outY)
	{
		GET_COMPONENT(BoxCollider2DComponent, entityID, (void)(*outX = 1, *outY = 1));
		Vec2 s = comp.GetScale(); *outX = s.x; *outY = s.y;
	}

	static void Bolt_BoxCollider2D_GetCenter(uint64_t entityID, float* outX, float* outY)
	{
		GET_COMPONENT(BoxCollider2DComponent, entityID, (void)(*outX = 0, *outY = 0));
		Vec2 c = comp.GetCenter(); *outX = c.x; *outY = c.y;
	}

	static void Bolt_BoxCollider2D_SetEnabled(uint64_t entityID, int enabled)
	{
		GET_COMPONENT(BoxCollider2DComponent, entityID, );
		comp.SetEnabled(enabled != 0);
	}

	// ── AudioSource ─────────────────────────────────────────────────────

	static void Bolt_AudioSource_Play(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, ); AudioManager::PlayAudioSource(comp); }
	static void Bolt_AudioSource_Pause(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, ); AudioManager::PauseAudioSource(comp); }
	static void Bolt_AudioSource_Stop(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, ); AudioManager::StopAudioSource(comp); }
	static void Bolt_AudioSource_Resume(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, ); AudioManager::ResumeAudioSource(comp); }

	static float Bolt_AudioSource_GetVolume(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, 1.0f); return comp.GetVolume(); }
	static void  Bolt_AudioSource_SetVolume(uint64_t entityID, float volume) { GET_COMPONENT(AudioSourceComponent, entityID, ); comp.SetVolume(volume); }
	static float Bolt_AudioSource_GetPitch(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, 1.0f); return comp.GetPitch(); }
	static void  Bolt_AudioSource_SetPitch(uint64_t entityID, float pitch) { GET_COMPONENT(AudioSourceComponent, entityID, ); comp.SetPitch(pitch); }
	static int   Bolt_AudioSource_GetLoop(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, 0); return comp.IsLooping() ? 1 : 0; }
	static void  Bolt_AudioSource_SetLoop(uint64_t entityID, int loop) { GET_COMPONENT(AudioSourceComponent, entityID, ); comp.SetLoop(loop != 0); }
	static int   Bolt_AudioSource_IsPlaying(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, 0); return comp.IsPlaying() ? 1 : 0; }
	static int   Bolt_AudioSource_IsPaused(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, 0); return comp.IsPaused() ? 1 : 0; }

	// ── Bolt-Physics ────────────────────────────────────────────────────

	static int Bolt_BoltBody2D_GetBodyType(uint64_t entityID) { GET_COMPONENT(BoltBody2DComponent, entityID, 1); return static_cast<int>(comp.Type); }
	static void Bolt_BoltBody2D_SetBodyType(uint64_t entityID, int type) { GET_COMPONENT(BoltBody2DComponent, entityID, ); comp.Type = static_cast<BoltPhys::BodyType>(type); if (comp.m_Body) comp.m_Body->SetBodyType(comp.Type); }
	static float Bolt_BoltBody2D_GetMass(uint64_t entityID) { GET_COMPONENT(BoltBody2DComponent, entityID, 1.0f); return comp.Mass; }
	static void Bolt_BoltBody2D_SetMass(uint64_t entityID, float mass) { GET_COMPONENT(BoltBody2DComponent, entityID, ); comp.Mass = mass; if (comp.m_Body) comp.m_Body->SetMass(mass); }
	static int Bolt_BoltBody2D_GetUseGravity(uint64_t entityID) { GET_COMPONENT(BoltBody2DComponent, entityID, 1); return comp.UseGravity ? 1 : 0; }
	static void Bolt_BoltBody2D_SetUseGravity(uint64_t entityID, int enabled) { GET_COMPONENT(BoltBody2DComponent, entityID, ); comp.UseGravity = enabled != 0; if (comp.m_Body) comp.m_Body->SetGravityEnabled(comp.UseGravity); }
	static void Bolt_BoltBody2D_GetVelocity(uint64_t entityID, float* outX, float* outY) { GET_COMPONENT(BoltBody2DComponent, entityID, (void)(*outX = 0, *outY = 0)); Vec2 v = comp.GetVelocity(); *outX = v.x; *outY = v.y; }
	static void Bolt_BoltBody2D_SetVelocity(uint64_t entityID, float x, float y) { GET_COMPONENT(BoltBody2DComponent, entityID, ); comp.SetVelocity({ x, y }); }

	static void Bolt_BoltBoxCollider2D_GetHalfExtents(uint64_t entityID, float* outX, float* outY) { GET_COMPONENT(BoltBoxCollider2DComponent, entityID, (void)(*outX = 0.5f, *outY = 0.5f)); *outX = comp.HalfExtents.x; *outY = comp.HalfExtents.y; }
	static void Bolt_BoltBoxCollider2D_SetHalfExtents(uint64_t entityID, float x, float y) { GET_COMPONENT(BoltBoxCollider2DComponent, entityID, ); comp.SetHalfExtents({ x, y }); }

	static float Bolt_BoltCircleCollider2D_GetRadius(uint64_t entityID) { GET_COMPONENT(BoltCircleCollider2DComponent, entityID, 0.5f); return comp.Radius; }
	static void Bolt_BoltCircleCollider2D_SetRadius(uint64_t entityID, float radius) { GET_COMPONENT(BoltCircleCollider2DComponent, entityID, ); comp.SetRadius(radius); }

	// ── ParticleSystem2D ────────────────────────────────────────────────

	static void Bolt_ParticleSystem2D_Play(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.Play();
	}
	static void Bolt_ParticleSystem2D_Pause(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.Pause();
	}
	static void Bolt_ParticleSystem2D_Stop(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.Stop();
	}
	static int Bolt_ParticleSystem2D_IsPlaying(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, 0);
		return comp.IsPlaying() ? 1 : 0;
	}
	static int Bolt_ParticleSystem2D_GetPlayOnAwake(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, 0);
		return comp.PlayOnAwake ? 1 : 0;
	}
	static void Bolt_ParticleSystem2D_SetPlayOnAwake(uint64_t entityID, int enabled) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.PlayOnAwake = (enabled != 0);
	}
	static void Bolt_ParticleSystem2D_GetColor(uint64_t entityID, float* r, float* g, float* b, float* a) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		*r = comp.RenderingSettings.Color.r; *g = comp.RenderingSettings.Color.g;
		*b = comp.RenderingSettings.Color.b; *a = comp.RenderingSettings.Color.a;
	}
	static void Bolt_ParticleSystem2D_SetColor(uint64_t entityID, float r, float g, float b, float a) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.RenderingSettings.Color = { r, g, b, a };
	}
	static float Bolt_ParticleSystem2D_GetLifeTime(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, 0.0f);
		return comp.ParticleSettings.LifeTime;
	}
	static void Bolt_ParticleSystem2D_SetLifeTime(uint64_t entityID, float lifetime) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.ParticleSettings.LifeTime = lifetime;
	}
	static float Bolt_ParticleSystem2D_GetSpeed(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, 0.0f);
		return comp.ParticleSettings.Speed;
	}
	static void Bolt_ParticleSystem2D_SetSpeed(uint64_t entityID, float speed) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.ParticleSettings.Speed = speed;
	}
	static float Bolt_ParticleSystem2D_GetScale(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, 0.0f);
		return comp.ParticleSettings.Scale;
	}
	static void Bolt_ParticleSystem2D_SetScale(uint64_t entityID, float scale) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.ParticleSettings.Scale = scale;
	}
	static int Bolt_ParticleSystem2D_GetEmitOverTime(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, 0);
		return comp.EmissionSettings.EmitOverTime;
	}
	static void Bolt_ParticleSystem2D_SetEmitOverTime(uint64_t entityID, int rate) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.EmissionSettings.EmitOverTime = static_cast<uint16_t>(rate);
	}
	static void Bolt_ParticleSystem2D_Emit(uint64_t entityID, int count) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.Emit(static_cast<size_t>(count));
	}

	// ── Gizmos ──────────────────────────────────────────────────────────

	static void Bolt_Gizmo_DrawLine(float x1, float y1, float x2, float y2) { Gizmo::DrawLine({ x1, y1 }, { x2, y2 }); }
	static void Bolt_Gizmo_DrawSquare(float cx, float cy, float sx, float sy, float degrees) { Gizmo::DrawSquare({ cx, cy }, { sx, sy }, degrees); }
	static void Bolt_Gizmo_DrawCircle(float cx, float cy, float radius, int segments) { Gizmo::DrawCircle({ cx, cy }, radius, segments); }
	static void Bolt_Gizmo_SetColor(float r, float g, float b, float a) { Gizmo::SetColor(Color(r, g, b, a)); }
	static void Bolt_Gizmo_GetColor(float* r, float* g, float* b, float* a) { Color c = Gizmo::GetColor(); *r = c.r; *g = c.g; *b = c.b; *a = c.a; }
	static float Bolt_Gizmo_GetLineWidth() { return Gizmo::GetLineWidth(); }
	static void Bolt_Gizmo_SetLineWidth(float width) { Gizmo::SetLineWidth(width); }

	// ── Physics2D ───────────────────────────────────────────────────────

	static int Bolt_Physics2D_Raycast(
		float originX, float originY, float dirX, float dirY, float distance,
		uint64_t* hitEntityID, float* hitX, float* hitY, float* hitNormalX, float* hitNormalY)
	{
		*hitEntityID = 0; *hitX = 0; *hitY = 0; *hitNormalX = 0; *hitNormalY = 0;

		auto result = Physics2D::Raycast({ originX, originY }, { dirX, dirY }, distance);
		if (result.has_value()) {
			Scene* scene = GetScene();
			if (scene && scene->IsValid(result->entity)) {
				*hitEntityID = GetEntityScriptId(*scene, result->entity);
			}
			else {
				SceneManager::Get().ForeachLoadedScene([&](const Scene& loadedScene) {
					if (*hitEntityID == 0 && loadedScene.IsValid(result->entity)) {
						*hitEntityID = GetEntityScriptId(loadedScene, result->entity);
					}
				});
			}
			*hitX = result->point.x; *hitY = result->point.y;
			*hitNormalX = result->normal.x; *hitNormalY = result->normal.y;
			return 1;
		}
		return 0;
	}

	#undef GET_COMPONENT

	// ── Registration ────────────────────────────────────────────────────

	void ScriptBindings::PopulateNativeBindings(NativeBindings& b)
	{
		PopulateNonComponentBindings(b);

		b.Entity_Clone = &Bolt_Entity_Clone;
		b.Entity_HasComponent = &Bolt_Entity_HasComponent;
		b.Entity_AddComponent = &Bolt_Entity_AddComponent;
		b.Entity_RemoveComponent = &Bolt_Entity_RemoveComponent;

		b.NameComponent_GetName = &Bolt_NameComponent_GetName;
		b.NameComponent_SetName = &Bolt_NameComponent_SetName;

		b.Transform2D_GetPosition = &Bolt_Transform2D_GetPosition;
		b.Transform2D_SetPosition = &Bolt_Transform2D_SetPosition;
		b.Transform2D_GetRotation = &Bolt_Transform2D_GetRotation;
		b.Transform2D_SetRotation = &Bolt_Transform2D_SetRotation;
		b.Transform2D_GetScale = &Bolt_Transform2D_GetScale;
		b.Transform2D_SetScale = &Bolt_Transform2D_SetScale;

		b.SpriteRenderer_GetColor = &Bolt_SpriteRenderer_GetColor;
		b.SpriteRenderer_SetColor = &Bolt_SpriteRenderer_SetColor;
		b.SpriteRenderer_GetTexture = &Bolt_SpriteRenderer_GetTexture;
		b.SpriteRenderer_SetTexture = &Bolt_SpriteRenderer_SetTexture;
		b.SpriteRenderer_GetSortingOrder = &Bolt_SpriteRenderer_GetSortingOrder;
		b.SpriteRenderer_SetSortingOrder = &Bolt_SpriteRenderer_SetSortingOrder;
		b.SpriteRenderer_GetSortingLayer = &Bolt_SpriteRenderer_GetSortingLayer;
		b.SpriteRenderer_SetSortingLayer = &Bolt_SpriteRenderer_SetSortingLayer;

		b.Camera2D_GetOrthographicSize = &Bolt_Camera2D_GetOrthographicSize;
		b.Camera2D_SetOrthographicSize = &Bolt_Camera2D_SetOrthographicSize;
		b.Camera2D_GetZoom = &Bolt_Camera2D_GetZoom;
		b.Camera2D_SetZoom = &Bolt_Camera2D_SetZoom;
		b.Camera2D_GetClearColor = &Bolt_Camera2D_GetClearColor;
		b.Camera2D_SetClearColor = &Bolt_Camera2D_SetClearColor;
		b.Camera2D_ScreenToWorld = &Bolt_Camera2D_ScreenToWorld;
		b.Camera2D_GetViewportWidth = &Bolt_Camera2D_GetViewportWidth;
		b.Camera2D_GetViewportHeight = &Bolt_Camera2D_GetViewportHeight;

		b.Rigidbody2D_ApplyForce = &Bolt_Rigidbody2D_ApplyForce;
		b.Rigidbody2D_ApplyImpulse = &Bolt_Rigidbody2D_ApplyImpulse;
		b.Rigidbody2D_GetLinearVelocity = &Bolt_Rigidbody2D_GetLinearVelocity;
		b.Rigidbody2D_SetLinearVelocity = &Bolt_Rigidbody2D_SetLinearVelocity;
		b.Rigidbody2D_GetAngularVelocity = &Bolt_Rigidbody2D_GetAngularVelocity;
		b.Rigidbody2D_SetAngularVelocity = &Bolt_Rigidbody2D_SetAngularVelocity;
		b.Rigidbody2D_GetBodyType = &Bolt_Rigidbody2D_GetBodyType;
		b.Rigidbody2D_SetBodyType = &Bolt_Rigidbody2D_SetBodyType;
		b.Rigidbody2D_GetGravityScale = &Bolt_Rigidbody2D_GetGravityScale;
		b.Rigidbody2D_SetGravityScale = &Bolt_Rigidbody2D_SetGravityScale;
		b.Rigidbody2D_GetMass = &Bolt_Rigidbody2D_GetMass;
		b.Rigidbody2D_SetMass = &Bolt_Rigidbody2D_SetMass;

		b.BoxCollider2D_GetScale = &Bolt_BoxCollider2D_GetScale;
		b.BoxCollider2D_GetCenter = &Bolt_BoxCollider2D_GetCenter;
		b.BoxCollider2D_SetEnabled = &Bolt_BoxCollider2D_SetEnabled;

		b.AudioSource_Play = &Bolt_AudioSource_Play;
		b.AudioSource_Pause = &Bolt_AudioSource_Pause;
		b.AudioSource_Stop = &Bolt_AudioSource_Stop;
		b.AudioSource_Resume = &Bolt_AudioSource_Resume;
		b.AudioSource_GetVolume = &Bolt_AudioSource_GetVolume;
		b.AudioSource_SetVolume = &Bolt_AudioSource_SetVolume;
		b.AudioSource_GetPitch = &Bolt_AudioSource_GetPitch;
		b.AudioSource_SetPitch = &Bolt_AudioSource_SetPitch;
		b.AudioSource_GetLoop = &Bolt_AudioSource_GetLoop;
		b.AudioSource_SetLoop = &Bolt_AudioSource_SetLoop;
		b.AudioSource_IsPlaying = &Bolt_AudioSource_IsPlaying;
		b.AudioSource_IsPaused = &Bolt_AudioSource_IsPaused;

		b.BoltBody2D_GetBodyType = &Bolt_BoltBody2D_GetBodyType;
		b.BoltBody2D_SetBodyType = &Bolt_BoltBody2D_SetBodyType;
		b.BoltBody2D_GetMass = &Bolt_BoltBody2D_GetMass;
		b.BoltBody2D_SetMass = &Bolt_BoltBody2D_SetMass;
		b.BoltBody2D_GetUseGravity = &Bolt_BoltBody2D_GetUseGravity;
		b.BoltBody2D_SetUseGravity = &Bolt_BoltBody2D_SetUseGravity;
		b.BoltBody2D_GetVelocity = &Bolt_BoltBody2D_GetVelocity;
		b.BoltBody2D_SetVelocity = &Bolt_BoltBody2D_SetVelocity;
		b.BoltBoxCollider2D_GetHalfExtents = &Bolt_BoltBoxCollider2D_GetHalfExtents;
		b.BoltBoxCollider2D_SetHalfExtents = &Bolt_BoltBoxCollider2D_SetHalfExtents;
		b.BoltCircleCollider2D_GetRadius = &Bolt_BoltCircleCollider2D_GetRadius;
		b.BoltCircleCollider2D_SetRadius = &Bolt_BoltCircleCollider2D_SetRadius;

		b.Scene_GetActiveSceneName = &Bolt_Scene_GetActiveSceneName;
		b.Scene_GetEntityCount = &Bolt_Scene_GetEntityCount;
		b.Scene_GetEntityNameByUUID = &Bolt_Scene_GetEntityNameByUUID;
		b.Scene_LoadAdditive = &Bolt_Scene_LoadAdditive;
		b.Scene_Load = &Bolt_Scene_Load;
		b.Scene_Unload = &Bolt_Scene_Unload;
		b.Scene_SetActive = &Bolt_Scene_SetActive;
		b.Scene_Reload = &Bolt_Scene_Reload;
		b.Scene_GetLoadedCount = &Bolt_Scene_GetLoadedCount;
		b.Scene_GetLoadedSceneNameAt = &Bolt_Scene_GetLoadedSceneNameAt;
		b.Scene_QueryEntities = &Bolt_Scene_QueryEntities;
		b.Scene_QueryEntitiesFiltered = &Bolt_Scene_QueryEntitiesFiltered;

		b.Asset_IsValid = &Bolt_Asset_IsValid;
		b.Asset_GetOrCreateUUIDFromPath = &Bolt_Asset_GetOrCreateUUIDFromPath;
		b.Asset_GetPath = &Bolt_Asset_GetPath;
		b.Asset_GetDisplayName = &Bolt_Asset_GetDisplayName;
		b.Texture_LoadAsset = &Bolt_Texture_LoadAsset;
		b.Texture_GetWidth = &Bolt_Texture_GetWidth;
		b.Texture_GetHeight = &Bolt_Texture_GetHeight;
		b.Audio_LoadAsset = &Bolt_Audio_LoadAsset;
		b.Audio_PlayOneShotAsset = &Bolt_Audio_PlayOneShotAsset;

		b.ParticleSystem2D_Play = &Bolt_ParticleSystem2D_Play;
		b.ParticleSystem2D_Pause = &Bolt_ParticleSystem2D_Pause;
		b.ParticleSystem2D_Stop = &Bolt_ParticleSystem2D_Stop;
		b.ParticleSystem2D_IsPlaying = &Bolt_ParticleSystem2D_IsPlaying;
		b.ParticleSystem2D_GetPlayOnAwake = &Bolt_ParticleSystem2D_GetPlayOnAwake;
		b.ParticleSystem2D_SetPlayOnAwake = &Bolt_ParticleSystem2D_SetPlayOnAwake;
		b.ParticleSystem2D_GetColor = &Bolt_ParticleSystem2D_GetColor;
		b.ParticleSystem2D_SetColor = &Bolt_ParticleSystem2D_SetColor;
		b.ParticleSystem2D_GetLifeTime = &Bolt_ParticleSystem2D_GetLifeTime;
		b.ParticleSystem2D_SetLifeTime = &Bolt_ParticleSystem2D_SetLifeTime;
		b.ParticleSystem2D_GetSpeed = &Bolt_ParticleSystem2D_GetSpeed;
		b.ParticleSystem2D_SetSpeed = &Bolt_ParticleSystem2D_SetSpeed;
		b.ParticleSystem2D_GetScale = &Bolt_ParticleSystem2D_GetScale;
		b.ParticleSystem2D_SetScale = &Bolt_ParticleSystem2D_SetScale;
		b.ParticleSystem2D_GetEmitOverTime = &Bolt_ParticleSystem2D_GetEmitOverTime;
		b.ParticleSystem2D_SetEmitOverTime = &Bolt_ParticleSystem2D_SetEmitOverTime;
		b.ParticleSystem2D_Emit = &Bolt_ParticleSystem2D_Emit;

		b.Gizmo_DrawLine = &Bolt_Gizmo_DrawLine;
		b.Gizmo_DrawSquare = &Bolt_Gizmo_DrawSquare;
		b.Gizmo_DrawCircle = &Bolt_Gizmo_DrawCircle;
		b.Gizmo_SetColor = &Bolt_Gizmo_SetColor;
		b.Gizmo_GetColor = &Bolt_Gizmo_GetColor;
		b.Gizmo_GetLineWidth = &Bolt_Gizmo_GetLineWidth;
		b.Gizmo_SetLineWidth = &Bolt_Gizmo_SetLineWidth;

		b.Physics2D_Raycast = &Bolt_Physics2D_Raycast;
	}

} // namespace Bolt
