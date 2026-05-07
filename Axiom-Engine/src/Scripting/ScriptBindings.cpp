#include "pch.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptComponent.hpp"
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
#include "Serialization/Json.hpp"
#include "Project/ProjectManager.hpp"
#include "Project/AxiomProject.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Components/General/UUIDComponent.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/General/NameComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
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
#include "Graphics/Text/FontManager.hpp"
#include "Graphics/Gizmo.hpp"
#include "Physics/Physics2D.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string_view>

namespace Axiom {
	bool ScriptBindings::IsScriptInputEnabled()
	{
		return Application::s_Instance && Application::s_Instance->m_IsScriptInputEnabled;
	}

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
		if (scene.IsValid(handle)) {
			const uint64_t runtimeId = scene.GetRuntimeID(handle);
			if (runtimeId != 0) {
				return runtimeId;
			}
		}

		return FromEntityHandle(handle);
	}

	bool TryResolveEntityByUUID(const Scene& scene, uint64_t entityID, EntityHandle& outHandle)
	{
		if (scene.TryResolveRuntimeID(entityID, outHandle)) {
			return true;
		}

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

		// Never fall back to ToEntityHandle(entityID) — UUIDs would truncate and silently alias entt handles.
		Scene* currentScene = ScriptEngine::GetScene();
		if (currentScene) {
			if (TryResolveEntityByUUID(*currentScene, entityID, outHandle)) {
				outScene = currentScene;
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
		if (scene && scene->IsLoaded()) {
			return scene;
		}

		auto* app = Application::GetInstance();
		if (app && app->GetSceneManager()) {
			return app->GetSceneManager()->GetActiveScene();
		}
		return nullptr;
	}

	// Legacy string callbacks use this per-thread scratch buffer. Managed callers use
	// the Buffer variants below so returned strings are copied into caller-owned memory.
	thread_local std::string s_StringReturnBuffer;

	static int CopyStringToBuffer(std::string_view value, char* outBuffer, int capacity)
	{
		const int requiredBytes = static_cast<int>(std::min(
			value.size(),
			static_cast<size_t>(std::numeric_limits<int>::max())));
		if (outBuffer && capacity > 0) {
			const int bytesToCopy = std::min(requiredBytes, capacity - 1);
			if (bytesToCopy > 0) {
				std::memcpy(outBuffer, value.data(), static_cast<size_t>(bytesToCopy));
			}
			outBuffer[bytesToCopy] = '\0';
		}
		return requiredBytes;
	}

	static int CopyCStringToBuffer(const char* value, char* outBuffer, int capacity)
	{
		return CopyStringToBuffer(value ? std::string_view(value) : std::string_view(), outBuffer, capacity);
	}

	void PopulateNonComponentBindings(NativeBindings& b);

	#define GET_COMPONENT(Type, entityID, failReturn) \
		Scene* scene = nullptr; \
		EntityHandle handle = entt::null; \
		if (!ResolveEntityReference(entityID, scene, handle)) return failReturn; \
		if (!scene->HasComponent<Type>(handle)) return failReturn; \
		auto& comp = scene->GetComponent<Type>(handle)


	// Tries displayName, serializedName, and the +"Component" C# fallbacks for package types.
	static const ComponentInfo* FindComponentByName(const std::string& name) {
		const auto& registry = SceneManager::Get().GetComponentRegistry();
		const ComponentInfo* found = nullptr;
		registry.ForEachComponentInfo([&](const std::type_index&, const ComponentInfo& info) {
			if (found) return;
			if (info.displayName == name) { found = &info; return; }
			if (!info.serializedName.empty() && info.serializedName == name) { found = &info; return; }
			if (!info.serializedName.empty() && (info.serializedName + "Component") == name) {
				found = &info;
				return;
			}
			if (!info.displayName.empty()) {
				std::string normalized;
				normalized.reserve(info.displayName.size());
				for (char c : info.displayName) {
					if (c != ' ') normalized.push_back(c);
				}
				if ((normalized + "Component") == name) {
					found = &info;
					return;
				}
			}
		});
		return found;
	}

	static const ComponentInfo* FindComponentByName(const char* name) {
		return FindComponentByName(std::string(name ? name : ""));
	}

	static bool HasManagedComponent(Scene& scene, EntityHandle handle, const std::string& className) {
		if (!scene.IsValid(handle) || !scene.HasComponent<ScriptComponent>(handle)) return false;
		return scene.GetComponent<ScriptComponent>(handle).HasManagedComponent(className);
	}

	static bool AddManagedComponent(Scene& scene, EntityHandle handle, const std::string& className) {
		if (!scene.IsValid(handle) || className.empty()) return false;
		if (!scene.HasComponent<ScriptComponent>(handle)) {
			scene.AddComponent<ScriptComponent>(handle);
		}
		auto& scriptComponent = scene.GetComponent<ScriptComponent>(handle);
		if (scriptComponent.HasManagedComponent(className)) return true;
		scriptComponent.AddManagedComponent(className);
		scene.MarkDirty();
		return true;
	}

	static bool RemoveManagedComponent(Scene& scene, EntityHandle handle, const std::string& className) {
		if (!scene.IsValid(handle) || !scene.HasComponent<ScriptComponent>(handle)) return false;
		const bool removed = scene.GetComponent<ScriptComponent>(handle).RemoveManagedComponent(className);
		if (removed) scene.MarkDirty();
		return removed;
	}

	static const char* Axiom_Entity_GetManagedComponentFields(uint64_t entityID, const char* componentName)
	{
		s_StringReturnBuffer = "{}";
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		const std::string className = componentName ? componentName : "";
		if (className.empty() || !ResolveEntityReference(entityID, scene, handle)
			|| !scene->HasComponent<ScriptComponent>(handle)) {
			return s_StringReturnBuffer.c_str();
		}

		const auto& scriptComponent = scene->GetComponent<ScriptComponent>(handle);
		if (!scriptComponent.HasManagedComponent(className)) {
			return s_StringReturnBuffer.c_str();
		}

		Json::Value root = Json::Value::MakeObject();
		const std::string prefix = className + ".";
		for (const auto& [key, value] : scriptComponent.PendingFieldValues) {
			if (key.rfind(prefix, 0) != 0) {
				continue;
			}
			root.AddMember(key.substr(prefix.size()), Json::Value(value));
		}

		s_StringReturnBuffer = Json::Stringify(root, false);
		return s_StringReturnBuffer.c_str();
	}

	static int Axiom_Entity_GetManagedComponentFieldsBuffer(uint64_t entityID, const char* componentName, char* outBuffer, int capacity)
	{
		return CopyCStringToBuffer(Axiom_Entity_GetManagedComponentFields(entityID, componentName), outBuffer, capacity);
	}

	static int Axiom_Entity_GetIsStatic(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		return scene->HasComponent<StaticTag>(handle) ? 1 : 0;
	}

	static void Axiom_Entity_SetIsStatic(uint64_t entityID, int isStatic)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return;

		const bool shouldBeStatic = isStatic != 0;
		const bool currentlyStatic = scene->HasComponent<StaticTag>(handle);
		if (shouldBeStatic == currentlyStatic) return;

		if (shouldBeStatic) {
			scene->AddComponent<StaticTag>(handle);
		}
		else {
			scene->RemoveComponent<StaticTag>(handle);
		}
		scene->MarkDirty();
	}

	static int Axiom_Entity_GetIsEnabled(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		return scene->HasComponent<DisabledTag>(handle) ? 0 : 1;
	}

	static void Axiom_Entity_SetIsEnabled(uint64_t entityID, int isEnabled)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return;

		const bool shouldBeEnabled = isEnabled != 0;
		const bool currentlyEnabled = !scene->HasComponent<DisabledTag>(handle);
		if (shouldBeEnabled == currentlyEnabled) return;

		if (shouldBeEnabled) {
			scene->RemoveComponent<DisabledTag>(handle);
		}
		else {
			scene->AddComponent<DisabledTag>(handle);
		}
		scene->MarkDirty();
	}

	struct QueryComponentRequirement {
		const ComponentInfo* Native = nullptr;
		std::string ManagedClassName;

		bool Has(Scene& scene, EntityHandle handle, Entity entity) const {
			if (Native && Native->has) {
				return Native->has(entity);
			}
			return HasManagedComponent(scene, handle, ManagedClassName);
		}
	};

	static bool BuildComponentRequirement(const std::string& name, QueryComponentRequirement& out) {
		if (name.empty()) {
			return false;
		}

		const ComponentInfo* info = FindComponentByName(name);
		if (info && info->has) {
			out.Native = info;
			return true;
		}

		out.ManagedClassName = name;
		return true;
	}

	int Axiom_Entity_HasComponent(uint64_t entityID, const char* componentName)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;

		const ComponentInfo* info = FindComponentByName(componentName);
		if (!info || !info->has) return HasManagedComponent(*scene, handle, componentName ? componentName : "") ? 1 : 0;
		return info->has(scene->GetEntity(handle)) ? 1 : 0;
	}

	int Axiom_Entity_AddComponent(uint64_t entityID, const char* componentName)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;

		const ComponentInfo* info = FindComponentByName(componentName);
		if (!info || !info->add) return AddManagedComponent(*scene, handle, componentName ? componentName : "") ? 1 : 0;

		Entity entity = scene->GetEntity(handle);
		if (info->has && info->has(entity)) return 1;
		info->add(entity);
		return 1;
	}

	int Axiom_Entity_RemoveComponent(uint64_t entityID, const char* componentName)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;

		const ComponentInfo* info = FindComponentByName(componentName);
		if (!info || !info->remove) return RemoveManagedComponent(*scene, handle, componentName ? componentName : "") ? 1 : 0;

		Entity entity = scene->GetEntity(handle);
		if (info->has && !info->has(entity)) return 0;
		info->remove(entity);
		return 1;
	}

	uint64_t Axiom_Entity_Clone(uint64_t sourceEntityID)
	{
		Scene* targetScene = GetScene();
		if (!targetScene) return 0;

		Scene* sourceScene = nullptr;
		EntityHandle sourceHandle = entt::null;
		if (!ResolveEntityReference(sourceEntityID, sourceScene, sourceHandle)) return 0;

		Entity source = sourceScene->GetEntity(sourceHandle);
		std::string name = source.GetName();

		Entity clone = targetScene->CreateRuntimeEntity(name + " (Clone)");

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

	uint64_t Axiom_Entity_InstantiatePrefab(uint64_t prefabGuid)
	{
		Scene* targetScene = GetScene();
		if (!targetScene) return 0;

		const EntityHandle instance = SceneSerializer::InstantiatePrefab(*targetScene, prefabGuid);
		return instance != entt::null ? GetEntityScriptId(*targetScene, instance) : 0;
	}

	int Axiom_Entity_GetOrigin(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return static_cast<int>(EntityOrigin::Runtime);
		return static_cast<int>(scene->GetEntityOrigin(handle));
	}

	uint64_t Axiom_Entity_GetRuntimeID(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		return scene->GetRuntimeID(handle);
	}

	uint64_t Axiom_Entity_GetSceneGUID(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		return static_cast<uint64_t>(scene->GetSceneEntityGUID(handle));
	}

	uint64_t Axiom_Entity_GetPrefabGUID(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		return static_cast<uint64_t>(scene->GetPrefabGUID(handle));
	}

	// ── Scene ───────────────────────────────────────────────────────────

	static const char* Axiom_Scene_GetActiveSceneName() {
		Scene* scene = GetScene();
		if (!scene) { s_StringReturnBuffer.clear(); return s_StringReturnBuffer.c_str(); }
		s_StringReturnBuffer = scene->GetName();
		return s_StringReturnBuffer.c_str();
	}

	static int Axiom_Scene_GetActiveSceneNameBuffer(char* outBuffer, int capacity)
	{
		return CopyCStringToBuffer(Axiom_Scene_GetActiveSceneName(), outBuffer, capacity);
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

	static int Axiom_Scene_GetEntityCount() {
		Scene* scene = GetScene();
		if (!scene) return 0;
		return CountSceneEntities(*scene);
	}

	static int Axiom_Scene_GetEntityCountByName(const char* sceneName) {
		if (!sceneName || sceneName[0] == '\0') return 0;
		auto scene = SceneManager::Get().GetLoadedScene(sceneName).lock();
		if (!scene || !scene->IsLoaded()) return 0;
		return CountSceneEntities(*scene);
	}

	static const char* Axiom_Scene_GetEntityNameByUUID(uint64_t uuid) {
		Scene* scene = GetScene();
		if (!scene) { s_StringReturnBuffer.clear(); return s_StringReturnBuffer.c_str(); }

		Scene* resolvedScene = nullptr;
		EntityHandle resolvedHandle = entt::null;
		if (ResolveEntityReference(uuid, resolvedScene, resolvedHandle)
			&& resolvedScene
			&& resolvedScene->HasComponent<NameComponent>(resolvedHandle)) {
			s_StringReturnBuffer = resolvedScene->GetComponent<NameComponent>(resolvedHandle).Name;
			return s_StringReturnBuffer.c_str();
		}

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

	static int Axiom_Scene_GetEntityNameByUUIDBuffer(uint64_t uuid, char* outBuffer, int capacity)
	{
		return CopyCStringToBuffer(Axiom_Scene_GetEntityNameByUUID(uuid), outBuffer, capacity);
	}

	static int LoadSceneByName(const char* sceneName, bool additive) {
		auto& sm = SceneManager::Get();
		std::string name(sceneName);
		AxiomProject* project = ProjectManager::GetCurrentProject();
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
			AIM_CORE_ERROR_TAG("ScriptBindings", "Failed to load scene{}: {}", additive ? " additively" : "", name);
			return 0;
		}

		AIM_INFO_TAG("ScriptBindings", "Loaded scene{}: {}", additive ? " additively" : "", name);
		return 1;
	}

	static int Axiom_Scene_LoadAdditive(const char* sceneName) {
		return LoadSceneByName(sceneName, true);
	}

	static int Axiom_Scene_Load(const char* sceneName) {
		return LoadSceneByName(sceneName, false);
	}

	static void Axiom_Scene_Unload(const char* sceneName) {
		SceneManager::Get().UnloadScene(sceneName);
	}

	static int Axiom_Scene_SetActive(const char* sceneName) {
		return SceneManager::Get().SetActiveScene(sceneName) ? 1 : 0;
	}

	static int Axiom_Scene_Reload(const char* sceneName) {
		auto result = SceneManager::Get().ReloadScene(sceneName);
		return result.lock() ? 1 : 0;
	}

	static int Axiom_Scene_SetGameSystemEnabled(const char* sceneName, const char* className, int enabled) {
		if (!sceneName || !className || sceneName[0] == '\0' || className[0] == '\0') return 0;
		auto scene = SceneManager::Get().GetLoadedScene(sceneName).lock();
		return scene && scene->SetGameSystemEnabled(className, enabled != 0) ? 1 : 0;
	}

	static void Axiom_Scene_SetGlobalSystemEnabled(const char* className, int enabled) {
		ScriptEngine::SetGlobalSystemEnabled(className ? className : "", enabled != 0);
	}

	static int Axiom_Scene_DoesSceneExist(const char* sceneName) {
		if (!sceneName || sceneName[0] == '\0') return 0;
		return SceneManager::Get().HasSceneDefinition(sceneName) ? 1 : 0;
	}

	static int Axiom_Scene_GetLoadedCount() {
		return static_cast<int>(SceneManager::Get().GetLoadedSceneCount());
	}

	static const char* Axiom_Scene_GetLoadedSceneNameAt(int index) {
		if (index < 0) {
			s_StringReturnBuffer.clear();
			return s_StringReturnBuffer.c_str();
		}

		const Scene* scene = SceneManager::Get().GetLoadedSceneAt(static_cast<size_t>(index));
		s_StringReturnBuffer = scene ? scene->GetName() : "";
		return s_StringReturnBuffer.c_str();
	}

	static int Axiom_Scene_GetLoadedSceneNameAtBuffer(int index, char* outBuffer, int capacity)
	{
		return CopyCStringToBuffer(Axiom_Scene_GetLoadedSceneNameAt(index), outBuffer, capacity);
	}

	// ── Scene Query ─────────────────────────────────────────────────────

	static Scene* ResolveLoadedSceneForQuery(const char* sceneName) {
		if (!sceneName || sceneName[0] == '\0') {
			return GetScene();
		}

		auto scene = SceneManager::Get().GetLoadedScene(sceneName).lock();
		return scene && scene->IsLoaded() ? scene.get() : nullptr;
	}

	static int QueryEntitiesInScene(Scene* scene, const char* componentNames, uint64_t* outEntityIDs, int maxOut) {
		if (!scene || !componentNames || !outEntityIDs || maxOut <= 0) return 0;
		if (!scene->IsLoaded()) return 0;

		// Parse pipe-delimited component names.
		std::vector<QueryComponentRequirement> requirements;
		std::string names(componentNames);
		size_t start = 0;
		while (start < names.size()) {
			size_t end = names.find('|', start);
			if (end == std::string::npos) end = names.size();
			std::string name = names.substr(start, end - start);
			QueryComponentRequirement requirement;
			if (!BuildComponentRequirement(name, requirement)) return 0;
			requirements.push_back(std::move(requirement));
			start = end + 1;
		}
		if (requirements.empty()) return 0;

		int count = 0;
		auto& registry = scene->GetRegistry();
		auto view = registry.view<entt::entity>();
		for (EntityHandle entityHandle : view) {
			if (!registry.valid(entityHandle)) continue;

			Entity entity = scene->GetEntity(entityHandle);
			bool match = true;
			for (const auto& requirement : requirements) {
				if (!requirement.Has(*scene, entityHandle, entity)) { match = false; break; }
			}
			if (match) {
				if (count < maxOut)
					outEntityIDs[count] = GetEntityScriptId(*scene, entityHandle);
				count++;
			}
		}
		return count;
	}

	static int Axiom_Scene_QueryEntities(const char* componentNames, uint64_t* outEntityIDs, int maxOut) {
		return QueryEntitiesInScene(GetScene(), componentNames, outEntityIDs, maxOut);
	}

	static int Axiom_Scene_QueryEntitiesInScene(const char* sceneName, const char* componentNames, uint64_t* outEntityIDs, int maxOut) {
		return QueryEntitiesInScene(ResolveLoadedSceneForQuery(sceneName), componentNames, outEntityIDs, maxOut);
	}

	static int Axiom_Asset_IsValid(uint64_t assetId)
	{
		return AssetRegistry::Exists(assetId) ? 1 : 0;
	}

	static uint64_t Axiom_Asset_GetOrCreateUUIDFromPath(const char* path)
	{
		if (!path || path[0] == '\0') {
			return 0;
		}

		return AssetRegistry::GetOrCreateAssetUUID(path);
	}

	static const char* Axiom_Asset_GetPath(uint64_t assetId)
	{
		s_StringReturnBuffer = AssetRegistry::ResolvePath(assetId);
		return s_StringReturnBuffer.c_str();
	}

	static int Axiom_Asset_GetPathBuffer(uint64_t assetId, char* outBuffer, int capacity)
	{
		return CopyCStringToBuffer(Axiom_Asset_GetPath(assetId), outBuffer, capacity);
	}

	static const char* Axiom_Asset_GetDisplayName(uint64_t assetId)
	{
		s_StringReturnBuffer = AssetRegistry::GetDisplayName(assetId);
		return s_StringReturnBuffer.c_str();
	}

	static int Axiom_Asset_GetDisplayNameBuffer(uint64_t assetId, char* outBuffer, int capacity)
	{
		return CopyCStringToBuffer(Axiom_Asset_GetDisplayName(assetId), outBuffer, capacity);
	}

	static int Axiom_Asset_GetKind(uint64_t assetId)
	{
		return static_cast<int>(AssetRegistry::GetKind(assetId));
	}

	static const char* Axiom_Asset_FindAll(const char* pathPrefix, int kind)
	{
		Json::Value ids = Json::Value::MakeArray();
		for (const AssetRegistry::Record& record : AssetRegistry::FindAll(
				 static_cast<AssetKind>(kind),
				 pathPrefix ? pathPrefix : "")) {
			ids.Append(Json::Value(std::to_string(record.Id)));
		}

		s_StringReturnBuffer = Json::Stringify(ids, false);
		return s_StringReturnBuffer.c_str();
	}

	static int Axiom_Asset_FindAllBuffer(const char* pathPrefix, int kind, char* outBuffer, int capacity)
	{
		return CopyCStringToBuffer(Axiom_Asset_FindAll(pathPrefix, kind), outBuffer, capacity);
	}

	static int Axiom_Texture_LoadAsset(uint64_t assetId)
	{
		return TextureManager::LoadTextureByUUID(assetId).IsValid() ? 1 : 0;
	}

	static int Axiom_Texture_GetWidth(uint64_t assetId)
	{
		TextureHandle handle = TextureManager::LoadTextureByUUID(assetId);
		Texture2D* texture = TextureManager::GetTexture(handle);
		return texture ? static_cast<int>(texture->GetWidth()) : 0;
	}

	static int Axiom_Texture_GetHeight(uint64_t assetId)
	{
		TextureHandle handle = TextureManager::LoadTextureByUUID(assetId);
		Texture2D* texture = TextureManager::GetTexture(handle);
		return texture ? static_cast<int>(texture->GetHeight()) : 0;
	}

	static int Axiom_Audio_LoadAsset(uint64_t assetId)
	{
		return AudioManager::LoadAudioByUUID(assetId).IsValid() ? 1 : 0;
	}

	static int Axiom_Font_LoadAsset(uint64_t assetId)
	{
		// Validity check only — DO NOT bake an atlas here. Atlas slots are
		// per (uuid, pixelSize) so picking a default size would waste a GL
		// texture every time `Font.IsValid` is called from script while the
		// consumer's actual FontSize differs. The TextRenderer system bakes
		// at TextRendererComponent::FontSize on first draw.
		return assetId != 0 && AssetRegistry::IsFont(assetId) ? 1 : 0;
	}

	static void Axiom_Audio_PlayOneShotAsset(uint64_t assetId, float volume)
	{
		AudioHandle handle = AudioManager::LoadAudioByUUID(assetId);
		if (handle.IsValid()) {
			AudioManager::PlayOneShot(handle, volume);
		}
	}

	// Helper: parse pipe-delimited names into native or managed component requirements.
	static bool ParseComponentNames(const char* names, std::vector<QueryComponentRequirement>& out) {
		if (!names || names[0] == '\0') return true; // empty is valid (no filter)
		std::string str(names);
		size_t start = 0;
		while (start < str.size()) {
			size_t end = str.find('|', start);
			if (end == std::string::npos) end = str.size();
			std::string name = str.substr(start, end - start);
			if (!name.empty()) {
				QueryComponentRequirement requirement;
				if (!BuildComponentRequirement(name, requirement)) return false;
				out.push_back(std::move(requirement));
			}
			start = end + 1;
		}
		return true;
	}

	static int QueryEntitiesFilteredInScene(
		Scene* scene,
		const char* withComponents,
		const char* withoutComponents,
		const char* mustHaveComponents,
		int enableFilter,
		uint64_t* outEntityIDs, int maxOut)
	{
		if (!scene || !outEntityIDs || maxOut <= 0) return 0;
		if (!scene->IsLoaded()) return 0;

		std::vector<QueryComponentRequirement> withInfos, withoutInfos, mustHaveInfos;
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
			for (const auto& requirement : withInfos) {
				if (!requirement.Has(*scene, entityHandle, entity)) { match = false; break; }
			}
			if (!match) continue;

			// MUST HAVE (With<>): entity must have these too
			for (const auto& requirement : mustHaveInfos) {
				if (!requirement.Has(*scene, entityHandle, entity)) { match = false; break; }
			}
			if (!match) continue;

			// WITHOUT: entity must NOT have any of these
			for (const auto& requirement : withoutInfos) {
				if (requirement.Has(*scene, entityHandle, entity)) { match = false; break; }
			}
			if (!match) continue;

			if (count < maxOut)
				outEntityIDs[count] = GetEntityScriptId(*scene, entityHandle);
			count++;
		}
		return count;
	}

	static int Axiom_Scene_QueryEntitiesFiltered(
		const char* withComponents,
		const char* withoutComponents,
		const char* mustHaveComponents,
		int enableFilter,
		uint64_t* outEntityIDs, int maxOut)
	{
		return QueryEntitiesFilteredInScene(GetScene(), withComponents, withoutComponents, mustHaveComponents, enableFilter, outEntityIDs, maxOut);
	}

	static int Axiom_Scene_QueryEntitiesFilteredInScene(
		const char* sceneName,
		const char* withComponents,
		const char* withoutComponents,
		const char* mustHaveComponents,
		int enableFilter,
		uint64_t* outEntityIDs, int maxOut)
	{
		return QueryEntitiesFilteredInScene(ResolveLoadedSceneForQuery(sceneName), withComponents, withoutComponents, mustHaveComponents, enableFilter, outEntityIDs, maxOut);
	}

	// ── NameComponent ───────────────────────────────────────────────────

	static const char* Axiom_NameComponent_GetName(uint64_t entityID)
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

	static int Axiom_NameComponent_GetNameBuffer(uint64_t entityID, char* outBuffer, int capacity)
	{
		return CopyCStringToBuffer(Axiom_NameComponent_GetName(entityID), outBuffer, capacity);
	}

	static void Axiom_NameComponent_SetName(uint64_t entityID, const char* name)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle) || !scene->HasComponent<NameComponent>(handle)) return;
		scene->GetComponent<NameComponent>(handle).Name = name;
	}

	// ── Transform2D ─────────────────────────────────────────────────────

	static void Axiom_Transform2D_GetPosition(uint64_t entityID, float* outX, float* outY)
	{
		GET_COMPONENT(Transform2DComponent, entityID, (void)(*outX = 0, *outY = 0));
		*outX = comp.Position.x; *outY = comp.Position.y;
	}

	static void Axiom_Transform2D_SetPosition(uint64_t entityID, float x, float y)
	{
		GET_COMPONENT(Transform2DComponent, entityID, );
		comp.SetPosition({ x, y });
	}

	static float Axiom_Transform2D_GetRotation(uint64_t entityID)
	{
		GET_COMPONENT(Transform2DComponent, entityID, 0.0f);
		return comp.Rotation;
	}

	static void Axiom_Transform2D_SetRotation(uint64_t entityID, float rotation)
	{
		GET_COMPONENT(Transform2DComponent, entityID, );
		comp.SetRotation(rotation);
	}

	static void Axiom_Transform2D_GetScale(uint64_t entityID, float* outX, float* outY)
	{
		GET_COMPONENT(Transform2DComponent, entityID, (void)(*outX = 1, *outY = 1));
		*outX = comp.Scale.x; *outY = comp.Scale.y;
	}

	static void Axiom_Transform2D_SetScale(uint64_t entityID, float x, float y)
	{
		GET_COMPONENT(Transform2DComponent, entityID, );
		comp.SetScale({ x, y });
	}

	// ── SpriteRenderer ──────────────────────────────────────────────────

	static void Axiom_SpriteRenderer_GetColor(uint64_t entityID, float* r, float* g, float* b, float* a)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, (void)(*r = 1, *g = 1, *b = 1, *a = 1));
		*r = comp.Color.r; *g = comp.Color.g; *b = comp.Color.b; *a = comp.Color.a;
	}

	static void Axiom_SpriteRenderer_SetColor(uint64_t entityID, float r, float g, float b, float a)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, );
		comp.Color = { r, g, b, a };
	}

	static uint64_t Axiom_SpriteRenderer_GetTexture(uint64_t entityID)
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

	static void Axiom_SpriteRenderer_SetTexture(uint64_t entityID, uint64_t assetId)
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

	static int Axiom_SpriteRenderer_GetSortingOrder(uint64_t entityID)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, 0);
		return comp.SortingOrder;
	}

	static void Axiom_SpriteRenderer_SetSortingOrder(uint64_t entityID, int order)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, );
		comp.SortingOrder = static_cast<short>(order);
	}

	static int Axiom_SpriteRenderer_GetSortingLayer(uint64_t entityID)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, 0);
		return comp.SortingLayer;
	}

	static void Axiom_SpriteRenderer_SetSortingLayer(uint64_t entityID, int layer)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, );
		comp.SortingLayer = static_cast<uint8_t>(layer);
	}

	// ── TextRenderer ────────────────────────────────────────────────────

	static const char* Axiom_TextRenderer_GetText(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle) || !scene->HasComponent<TextRendererComponent>(handle)) {
			s_StringReturnBuffer.clear();
			return s_StringReturnBuffer.c_str();
		}
		s_StringReturnBuffer = scene->GetComponent<TextRendererComponent>(handle).Text;
		return s_StringReturnBuffer.c_str();
	}

	static int Axiom_TextRenderer_GetTextBuffer(uint64_t entityID, char* outBuffer, int capacity)
	{
		return CopyCStringToBuffer(Axiom_TextRenderer_GetText(entityID), outBuffer, capacity);
	}

	static void Axiom_TextRenderer_SetText(uint64_t entityID, const char* text)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.Text = text ? text : "";
	}

	static uint64_t Axiom_TextRenderer_GetFont(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 0);

		uint64_t assetId = static_cast<uint64_t>(comp.FontAssetId);
		if (assetId == 0 && FontManager::IsValid(comp.ResolvedFont)) {
			assetId = FontManager::GetFontAssetUUID(comp.ResolvedFont);
			if (assetId != 0) {
				comp.FontAssetId = UUID(assetId);
			}
		}
		return assetId;
	}

	static void Axiom_TextRenderer_SetFont(uint64_t entityID, uint64_t assetId)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );

		comp.FontAssetId = UUID(assetId);
		// Force the renderer to re-resolve next frame at the current FontSize.
		comp.ResolvedFont = FontHandle{};
	}

	static float Axiom_TextRenderer_GetFontSize(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 32.0f);
		return comp.FontSize;
	}

	static void Axiom_TextRenderer_SetFontSize(uint64_t entityID, float size)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.FontSize = size;
		// A new pixel size needs a different atlas slot — invalidate the cache.
		comp.ResolvedFont = FontHandle{};
	}

	static void Axiom_TextRenderer_GetColor(uint64_t entityID, float* r, float* g, float* b, float* a)
	{
		GET_COMPONENT(TextRendererComponent, entityID, (void)(*r = 1, *g = 1, *b = 1, *a = 1));
		*r = comp.Color.r; *g = comp.Color.g; *b = comp.Color.b; *a = comp.Color.a;
	}

	static void Axiom_TextRenderer_SetColor(uint64_t entityID, float r, float g, float b, float a)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.Color = { r, g, b, a };
	}

	static float Axiom_TextRenderer_GetLetterSpacing(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 0.0f);
		return comp.LetterSpacing;
	}

	static void Axiom_TextRenderer_SetLetterSpacing(uint64_t entityID, float spacing)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.LetterSpacing = spacing;
	}

	static int Axiom_TextRenderer_GetHAlign(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 0);
		return static_cast<int>(comp.HAlign);
	}

	static void Axiom_TextRenderer_SetHAlign(uint64_t entityID, int align)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.HAlign = static_cast<TextAlignment>(align);
	}

	static int Axiom_TextRenderer_GetSortingOrder(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 0);
		return comp.SortingOrder;
	}

	static void Axiom_TextRenderer_SetSortingOrder(uint64_t entityID, int order)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.SortingOrder = static_cast<int16_t>(order);
	}

	static int Axiom_TextRenderer_GetSortingLayer(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 0);
		return comp.SortingLayer;
	}

	static void Axiom_TextRenderer_SetSortingLayer(uint64_t entityID, int layer)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.SortingLayer = static_cast<uint8_t>(layer);
	}

	// ── Camera2D ────────────────────────────────────────────────────────

	static float Axiom_Camera2D_GetOrthographicSize(uint64_t entityID)
	{
		GET_COMPONENT(Camera2DComponent, entityID, 5.0f);
		return comp.GetOrthographicSize();
	}

	static void Axiom_Camera2D_SetOrthographicSize(uint64_t entityID, float size)
	{
		GET_COMPONENT(Camera2DComponent, entityID, );
		comp.SetOrthographicSize(size);
	}

	static float Axiom_Camera2D_GetZoom(uint64_t entityID)
	{
		GET_COMPONENT(Camera2DComponent, entityID, 1.0f);
		return comp.GetZoom();
	}

	static void Axiom_Camera2D_SetZoom(uint64_t entityID, float zoom)
	{
		GET_COMPONENT(Camera2DComponent, entityID, );
		comp.SetZoom(zoom);
	}

	static void Axiom_Camera2D_GetClearColor(uint64_t entityID, float* r, float* g, float* b, float* a)
	{
		GET_COMPONENT(Camera2DComponent, entityID, (void)(*r = 0.1f, *g = 0.1f, *b = 0.1f, *a = 1.0f));
		const auto& cc = comp.GetClearColor();
		*r = cc.r; *g = cc.g; *b = cc.b; *a = cc.a;
	}

	static void Axiom_Camera2D_SetClearColor(uint64_t entityID, float r, float g, float b, float a)
	{
		GET_COMPONENT(Camera2DComponent, entityID, );
		comp.SetClearColor(Color(r, g, b, a));
	}

	static void Axiom_Camera2D_ScreenToWorld(uint64_t entityID, float sx, float sy, float* outX, float* outY)
	{
		GET_COMPONENT(Camera2DComponent, entityID, (void)(*outX = 0, *outY = 0));
		Vec2 world = comp.ScreenToWorld({ sx, sy });
		*outX = world.x; *outY = world.y;
	}

	static float Axiom_Camera2D_GetViewportWidth(uint64_t entityID)
	{
		GET_COMPONENT(Camera2DComponent, entityID, 0.0f);
		return comp.ViewportWidth();
	}

	static float Axiom_Camera2D_GetViewportHeight(uint64_t entityID)
	{
		GET_COMPONENT(Camera2DComponent, entityID, 0.0f);
		return comp.ViewportHeight();
	}

	static uint64_t Axiom_Camera2D_GetMainEntity()
	{
		Scene* scene = GetScene();
		if (!scene) return 0;

		const EntityHandle handle = scene->GetMainCameraEntity();
		if (handle == entt::null) return 0;
		return GetEntityScriptId(*scene, handle);
	}

	// ── Rigidbody2D ─────────────────────────────────────────────────────

	static void Axiom_Rigidbody2D_ApplyForce(uint64_t entityID, float forceX, float forceY, int wake)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		b2BodyId bodyId = comp.GetBodyHandle();
		if (b2Body_IsValid(bodyId)) b2Body_ApplyForceToCenter(bodyId, { forceX, forceY }, wake != 0);
	}

	static void Axiom_Rigidbody2D_ApplyImpulse(uint64_t entityID, float impulseX, float impulseY, int wake)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		b2BodyId bodyId = comp.GetBodyHandle();
		if (b2Body_IsValid(bodyId)) b2Body_ApplyLinearImpulseToCenter(bodyId, { impulseX, impulseY }, wake != 0);
	}

	static void Axiom_Rigidbody2D_GetLinearVelocity(uint64_t entityID, float* outX, float* outY)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, (void)(*outX = 0, *outY = 0));
		Vec2 vel = comp.GetVelocity(); *outX = vel.x; *outY = vel.y;
	}

	static void Axiom_Rigidbody2D_SetLinearVelocity(uint64_t entityID, float x, float y)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		comp.SetVelocity({ x, y });
	}

	static float Axiom_Rigidbody2D_GetAngularVelocity(uint64_t entityID)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, 0.0f);
		return comp.GetAngularVelocity();
	}

	static void Axiom_Rigidbody2D_SetAngularVelocity(uint64_t entityID, float velocity)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		comp.SetAngularVelocity(velocity);
	}

	static int Axiom_Rigidbody2D_GetBodyType(uint64_t entityID)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, 0);
		return static_cast<int>(comp.GetBodyType());
	}

	static void Axiom_Rigidbody2D_SetBodyType(uint64_t entityID, int type)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		comp.SetBodyType(static_cast<BodyType>(type));
	}

	static float Axiom_Rigidbody2D_GetGravityScale(uint64_t entityID)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, 1.0f);
		return comp.GetGravityScale();
	}

	static void Axiom_Rigidbody2D_SetGravityScale(uint64_t entityID, float scale)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		comp.SetGravityScale(scale);
	}

	static float Axiom_Rigidbody2D_GetMass(uint64_t entityID)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, 1.0f);
		return comp.GetMass();
	}

	static void Axiom_Rigidbody2D_SetMass(uint64_t entityID, float mass)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		comp.SetMass(mass);
	}

	// ── BoxCollider2D ───────────────────────────────────────────────────

	static void Axiom_BoxCollider2D_GetScale(uint64_t entityID, float* outX, float* outY)
	{
		GET_COMPONENT(BoxCollider2DComponent, entityID, (void)(*outX = 1, *outY = 1));
		Vec2 s = comp.GetScale(); *outX = s.x; *outY = s.y;
	}

	static void Axiom_BoxCollider2D_GetCenter(uint64_t entityID, float* outX, float* outY)
	{
		GET_COMPONENT(BoxCollider2DComponent, entityID, (void)(*outX = 0, *outY = 0));
		Vec2 c = comp.GetCenter(); *outX = c.x; *outY = c.y;
	}

	static void Axiom_BoxCollider2D_SetEnabled(uint64_t entityID, int enabled)
	{
		GET_COMPONENT(BoxCollider2DComponent, entityID, );
		comp.SetEnabled(enabled != 0);
	}

	// ── AudioSource ─────────────────────────────────────────────────────

	static void Axiom_AudioSource_Play(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, ); AudioManager::PlayAudioSource(comp); }
	static void Axiom_AudioSource_Pause(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, ); AudioManager::PauseAudioSource(comp); }
	static void Axiom_AudioSource_Stop(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, ); AudioManager::StopAudioSource(comp); }
	static void Axiom_AudioSource_Resume(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, ); AudioManager::ResumeAudioSource(comp); }

	static float Axiom_AudioSource_GetVolume(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, 1.0f); return comp.GetVolume(); }
	static void  Axiom_AudioSource_SetVolume(uint64_t entityID, float volume) { GET_COMPONENT(AudioSourceComponent, entityID, ); comp.SetVolume(volume); }
	static float Axiom_AudioSource_GetPitch(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, 1.0f); return comp.GetPitch(); }
	static void  Axiom_AudioSource_SetPitch(uint64_t entityID, float pitch) { GET_COMPONENT(AudioSourceComponent, entityID, ); comp.SetPitch(pitch); }
	static int   Axiom_AudioSource_GetLoop(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, 0); return comp.IsLooping() ? 1 : 0; }
	static void  Axiom_AudioSource_SetLoop(uint64_t entityID, int loop) { GET_COMPONENT(AudioSourceComponent, entityID, ); comp.SetLoop(loop != 0); }
	static int   Axiom_AudioSource_IsPlaying(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, 0); return comp.IsPlaying() ? 1 : 0; }
	static int   Axiom_AudioSource_IsPaused(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, 0); return comp.IsPaused() ? 1 : 0; }

	static uint64_t Axiom_AudioSource_GetAudio(uint64_t entityID)
	{
		GET_COMPONENT(AudioSourceComponent, entityID, 0);

		// Cache UUID resolved from a loaded handle so subsequent calls are O(1).
		uint64_t assetId = static_cast<uint64_t>(comp.GetAudioAssetId());
		if (assetId == 0 && comp.GetAudioHandle().IsValid()) {
			assetId = AudioManager::GetAudioAssetUUID(comp.GetAudioHandle());
			if (assetId != 0) {
				comp.SetAudioAssetId(UUID(assetId));
			}
		}
		return assetId;
	}

	static void Axiom_AudioSource_SetAudio(uint64_t entityID, uint64_t assetId)
	{
		GET_COMPONENT(AudioSourceComponent, entityID, );

		if (assetId == 0) {
			comp.SetAudioHandle(AudioHandle(), UUID(0));
			return;
		}

		comp.SetAudioHandle(AudioManager::LoadAudioByUUID(assetId), UUID(assetId));
	}

	// ── Axiom-Physics ────────────────────────────────────────────────────

	static int Axiom_FastBody2D_GetBodyType(uint64_t entityID) { GET_COMPONENT(FastBody2DComponent, entityID, 1); return static_cast<int>(comp.Type); }
	static void Axiom_FastBody2D_SetBodyType(uint64_t entityID, int type) { GET_COMPONENT(FastBody2DComponent, entityID, ); comp.Type = static_cast<AxiomPhys::BodyType>(type); if (comp.m_Body) comp.m_Body->SetBodyType(comp.Type); }
	static float Axiom_FastBody2D_GetMass(uint64_t entityID) { GET_COMPONENT(FastBody2DComponent, entityID, 1.0f); return comp.Mass; }
	static void Axiom_FastBody2D_SetMass(uint64_t entityID, float mass) { GET_COMPONENT(FastBody2DComponent, entityID, ); comp.Mass = mass; if (comp.m_Body) comp.m_Body->SetMass(mass); }
	static int Axiom_FastBody2D_GetUseGravity(uint64_t entityID) { GET_COMPONENT(FastBody2DComponent, entityID, 1); return comp.UseGravity ? 1 : 0; }
	static void Axiom_FastBody2D_SetUseGravity(uint64_t entityID, int enabled) { GET_COMPONENT(FastBody2DComponent, entityID, ); comp.UseGravity = enabled != 0; if (comp.m_Body) comp.m_Body->SetGravityEnabled(comp.UseGravity); }
	static void Axiom_FastBody2D_GetVelocity(uint64_t entityID, float* outX, float* outY) { GET_COMPONENT(FastBody2DComponent, entityID, (void)(*outX = 0, *outY = 0)); Vec2 v = comp.GetVelocity(); *outX = v.x; *outY = v.y; }
	static void Axiom_FastBody2D_SetVelocity(uint64_t entityID, float x, float y) { GET_COMPONENT(FastBody2DComponent, entityID, ); comp.SetVelocity({ x, y }); }

	static void Axiom_FastBoxCollider2D_GetHalfExtents(uint64_t entityID, float* outX, float* outY) { GET_COMPONENT(FastBoxCollider2DComponent, entityID, (void)(*outX = 0.5f, *outY = 0.5f)); *outX = comp.HalfExtents.x; *outY = comp.HalfExtents.y; }
	static void Axiom_FastBoxCollider2D_SetHalfExtents(uint64_t entityID, float x, float y) { GET_COMPONENT(FastBoxCollider2DComponent, entityID, ); comp.SetHalfExtents({ x, y }); }

	static float Axiom_FastCircleCollider2D_GetRadius(uint64_t entityID) { GET_COMPONENT(FastCircleCollider2DComponent, entityID, 0.5f); return comp.Radius; }
	static void Axiom_FastCircleCollider2D_SetRadius(uint64_t entityID, float radius) { GET_COMPONENT(FastCircleCollider2DComponent, entityID, ); comp.SetRadius(radius); }

	// ── ParticleSystem2D ────────────────────────────────────────────────

	static void Axiom_ParticleSystem2D_Play(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.Play();
	}
	static void Axiom_ParticleSystem2D_Pause(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.Pause();
	}
	static void Axiom_ParticleSystem2D_Stop(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.Stop();
	}
	static int Axiom_ParticleSystem2D_IsPlaying(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, 0);
		return comp.IsPlaying() ? 1 : 0;
	}
	static int Axiom_ParticleSystem2D_GetPlayOnAwake(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, 0);
		return comp.PlayOnAwake ? 1 : 0;
	}
	static void Axiom_ParticleSystem2D_SetPlayOnAwake(uint64_t entityID, int enabled) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.PlayOnAwake = (enabled != 0);
	}
	static void Axiom_ParticleSystem2D_GetColor(uint64_t entityID, float* r, float* g, float* b, float* a) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		*r = comp.RenderingSettings.Color.r; *g = comp.RenderingSettings.Color.g;
		*b = comp.RenderingSettings.Color.b; *a = comp.RenderingSettings.Color.a;
	}
	static void Axiom_ParticleSystem2D_SetColor(uint64_t entityID, float r, float g, float b, float a) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.RenderingSettings.Color = { r, g, b, a };
	}
	static float Axiom_ParticleSystem2D_GetLifeTime(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, 0.0f);
		return comp.ParticleSettings.LifeTime;
	}
	static void Axiom_ParticleSystem2D_SetLifeTime(uint64_t entityID, float lifetime) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.ParticleSettings.LifeTime = lifetime;
	}
	static float Axiom_ParticleSystem2D_GetSpeed(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, 0.0f);
		return comp.ParticleSettings.Speed;
	}
	static void Axiom_ParticleSystem2D_SetSpeed(uint64_t entityID, float speed) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.ParticleSettings.Speed = speed;
	}
	static float Axiom_ParticleSystem2D_GetScale(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, 0.0f);
		return comp.ParticleSettings.Scale;
	}
	static void Axiom_ParticleSystem2D_SetScale(uint64_t entityID, float scale) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.ParticleSettings.Scale = scale;
	}
	static int Axiom_ParticleSystem2D_GetEmitOverTime(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, 0);
		return comp.EmissionSettings.EmitOverTime;
	}
	static void Axiom_ParticleSystem2D_SetEmitOverTime(uint64_t entityID, int rate) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.EmissionSettings.EmitOverTime = static_cast<uint16_t>(rate);
	}
	static void Axiom_ParticleSystem2D_Emit(uint64_t entityID, int count) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.Emit(static_cast<size_t>(count));
	}

	// ── Gizmos ──────────────────────────────────────────────────────────

	static void Axiom_Gizmo_DrawLine(float x1, float y1, float x2, float y2) { Gizmo::DrawLine({ x1, y1 }, { x2, y2 }); }
	static void Axiom_Gizmo_DrawSquare(float cx, float cy, float sx, float sy, float degrees) { Gizmo::DrawSquare({ cx, cy }, { sx, sy }, degrees); }
	static void Axiom_Gizmo_DrawCircle(float cx, float cy, float radius, int segments) { Gizmo::DrawCircle({ cx, cy }, radius, segments); }
	static void Axiom_Gizmo_SetColor(float r, float g, float b, float a) { Gizmo::SetColor(Color(r, g, b, a)); }
	static void Axiom_Gizmo_GetColor(float* r, float* g, float* b, float* a) { Color c = Gizmo::GetColor(); *r = c.r; *g = c.g; *b = c.b; *a = c.a; }
	static float Axiom_Gizmo_GetLineWidth() { return Gizmo::GetLineWidth(); }
	static void Axiom_Gizmo_SetLineWidth(float width) { Gizmo::SetLineWidth(width); }

	// ── Physics2D ───────────────────────────────────────────────────────

	static int Axiom_Physics2D_Raycast(
		float originX, float originY, float dirX, float dirY, float distance,
		uint64_t* hitEntityID, float* hitX, float* hitY, float* hitNormalX, float* hitNormalY, float* hitDistance)
	{
		if (!hitEntityID || !hitX || !hitY || !hitNormalX || !hitNormalY || !hitDistance) return 0;
		*hitEntityID = 0; *hitX = 0; *hitY = 0; *hitNormalX = 0; *hitNormalY = 0; *hitDistance = 0;

		auto result = Physics2D::Raycast({ originX, originY }, { dirX, dirY }, distance);
		if (result.has_value()) {
			if (result->scene && result->scene->IsValid(result->entity)) {
				*hitEntityID = GetEntityScriptId(*result->scene, result->entity);
			}
			*hitX = result->point.x; *hitY = result->point.y;
			*hitNormalX = result->normal.x; *hitNormalY = result->normal.y;
			*hitDistance = result->distance;
			return 1;
		}
		return 0;
	}

	static uint64_t ToScriptEntityId(const PhysicsBodyRef2D& ref) {
		if (!ref.scene || ref.entity == entt::null || !ref.scene->IsValid(ref.entity)) {
			return 0;
		}
		return GetEntityScriptId(*ref.scene, ref.entity);
	}

	static OverlapMode ToOverlapMode(int mode) {
		return mode == 1 ? OverlapMode::Nearest : OverlapMode::First;
	}

	static std::vector<Vec2> ReadPolygonPoints(const float* points, int pointCount) {
		std::vector<Vec2> result;
		constexpr int maxPolygonVertices = 8;
		if (!points || pointCount < 3 || pointCount > maxPolygonVertices) {
			return result;
		}

		result.reserve(static_cast<size_t>(pointCount));
		for (int i = 0; i < pointCount; ++i) {
			result.push_back({ points[i * 2], points[i * 2 + 1] });
		}
		return result;
	}

	static int WriteOverlapResult(std::optional<PhysicsBodyRef2D> result, uint64_t* entityID) {
		if (!entityID) return 0;
		*entityID = result.has_value() ? ToScriptEntityId(*result) : 0;
		return *entityID != 0 ? 1 : 0;
	}

	static int WriteOverlapResults(const std::vector<PhysicsBodyRef2D>& refs, uint64_t* outEntityIDs, int maxOut) {
		if (!outEntityIDs || maxOut <= 0) return 0;

		int count = 0;
		for (const PhysicsBodyRef2D& ref : refs) {
			uint64_t id = ToScriptEntityId(ref);
			if (id == 0) continue;

			if (count < maxOut) {
				outEntityIDs[count] = id;
			}
			++count;
		}
		return count;
	}

	static int Axiom_Physics2D_OverlapCircle(float originX, float originY, float radius, int mode, uint64_t* entityID) {
		return WriteOverlapResult(
			Physics2D::OverlapCircleRef({ originX, originY }, radius, ToOverlapMode(mode)),
			entityID);
	}

	static int Axiom_Physics2D_OverlapBox(float originX, float originY, float halfX, float halfY, float degrees, int mode, uint64_t* entityID) {
		return WriteOverlapResult(
			Physics2D::OverlapBoxRef({ originX, originY }, { halfX, halfY }, degrees, ToOverlapMode(mode)),
			entityID);
	}

	static int Axiom_Physics2D_OverlapPolygon(float originX, float originY, const float* points, int pointCount, int mode, uint64_t* entityID) {
		std::vector<Vec2> polygon = ReadPolygonPoints(points, pointCount);
		if (polygon.empty()) {
			if (entityID) *entityID = 0;
			return 0;
		}

		return WriteOverlapResult(
			Physics2D::OverlapPolygonRef({ originX, originY }, polygon, ToOverlapMode(mode)),
			entityID);
	}

	static int Axiom_Physics2D_OverlapCircleAll(float originX, float originY, float radius, uint64_t* outEntityIDs, int maxOut) {
		return WriteOverlapResults(
			Physics2D::OverlapCircleAllRefs({ originX, originY }, radius),
			outEntityIDs, maxOut);
	}

	static int Axiom_Physics2D_OverlapBoxAll(float originX, float originY, float halfX, float halfY, float degrees, uint64_t* outEntityIDs, int maxOut) {
		return WriteOverlapResults(
			Physics2D::OverlapBoxAllRefs({ originX, originY }, { halfX, halfY }, degrees),
			outEntityIDs, maxOut);
	}

	static int Axiom_Physics2D_OverlapPolygonAll(float originX, float originY, const float* points, int pointCount, uint64_t* outEntityIDs, int maxOut) {
		std::vector<Vec2> polygon = ReadPolygonPoints(points, pointCount);
		if (polygon.empty()) {
			return 0;
		}

		return WriteOverlapResults(
			Physics2D::OverlapPolygonAllRefs({ originX, originY }, polygon),
			outEntityIDs, maxOut);
	}

	static int Axiom_Physics2D_ContainsPoint(float originX, float originY, int mode, uint64_t* entityID) {
		return WriteOverlapResult(
			Physics2D::ContainsPointRef({ originX, originY }, ToOverlapMode(mode)),
			entityID);
	}

	static int Axiom_Physics2D_ContainsPointAll(float originX, float originY, uint64_t* outEntityIDs, int maxOut) {
		return WriteOverlapResults(
			Physics2D::ContainsPointAllRefs({ originX, originY }),
			outEntityIDs, maxOut);
	}

	#undef GET_COMPONENT

	// ── Registration ────────────────────────────────────────────────────

	void ScriptBindings::PopulateNativeBindings(NativeBindings& b)
	{
		PopulateNonComponentBindings(b);

		b.Entity_Clone = &Axiom_Entity_Clone;
		b.Entity_InstantiatePrefab = &Axiom_Entity_InstantiatePrefab;
		b.Entity_GetOrigin = &Axiom_Entity_GetOrigin;
		b.Entity_GetRuntimeID = &Axiom_Entity_GetRuntimeID;
		b.Entity_GetSceneGUID = &Axiom_Entity_GetSceneGUID;
		b.Entity_GetPrefabGUID = &Axiom_Entity_GetPrefabGUID;
		b.Entity_HasComponent = &Axiom_Entity_HasComponent;
		b.Entity_AddComponent = &Axiom_Entity_AddComponent;
		b.Entity_RemoveComponent = &Axiom_Entity_RemoveComponent;
		b.Entity_GetManagedComponentFields = &Axiom_Entity_GetManagedComponentFields;
		b.Entity_GetManagedComponentFieldsBuffer = &Axiom_Entity_GetManagedComponentFieldsBuffer;
		b.Entity_GetIsStatic = &Axiom_Entity_GetIsStatic;
		b.Entity_SetIsStatic = &Axiom_Entity_SetIsStatic;
		b.Entity_GetIsEnabled = &Axiom_Entity_GetIsEnabled;
		b.Entity_SetIsEnabled = &Axiom_Entity_SetIsEnabled;

		b.NameComponent_GetName = &Axiom_NameComponent_GetName;
		b.NameComponent_GetNameBuffer = &Axiom_NameComponent_GetNameBuffer;
		b.NameComponent_SetName = &Axiom_NameComponent_SetName;

		b.Transform2D_GetPosition = &Axiom_Transform2D_GetPosition;
		b.Transform2D_SetPosition = &Axiom_Transform2D_SetPosition;
		b.Transform2D_GetRotation = &Axiom_Transform2D_GetRotation;
		b.Transform2D_SetRotation = &Axiom_Transform2D_SetRotation;
		b.Transform2D_GetScale = &Axiom_Transform2D_GetScale;
		b.Transform2D_SetScale = &Axiom_Transform2D_SetScale;

		b.SpriteRenderer_GetColor = &Axiom_SpriteRenderer_GetColor;
		b.SpriteRenderer_SetColor = &Axiom_SpriteRenderer_SetColor;
		b.SpriteRenderer_GetTexture = &Axiom_SpriteRenderer_GetTexture;
		b.SpriteRenderer_SetTexture = &Axiom_SpriteRenderer_SetTexture;
		b.SpriteRenderer_GetSortingOrder = &Axiom_SpriteRenderer_GetSortingOrder;
		b.SpriteRenderer_SetSortingOrder = &Axiom_SpriteRenderer_SetSortingOrder;
		b.SpriteRenderer_GetSortingLayer = &Axiom_SpriteRenderer_GetSortingLayer;
		b.SpriteRenderer_SetSortingLayer = &Axiom_SpriteRenderer_SetSortingLayer;

		b.TextRenderer_GetText = &Axiom_TextRenderer_GetText;
		b.TextRenderer_GetTextBuffer = &Axiom_TextRenderer_GetTextBuffer;
		b.TextRenderer_SetText = &Axiom_TextRenderer_SetText;
		b.TextRenderer_GetFont = &Axiom_TextRenderer_GetFont;
		b.TextRenderer_SetFont = &Axiom_TextRenderer_SetFont;
		b.TextRenderer_GetFontSize = &Axiom_TextRenderer_GetFontSize;
		b.TextRenderer_SetFontSize = &Axiom_TextRenderer_SetFontSize;
		b.TextRenderer_GetColor = &Axiom_TextRenderer_GetColor;
		b.TextRenderer_SetColor = &Axiom_TextRenderer_SetColor;
		b.TextRenderer_GetLetterSpacing = &Axiom_TextRenderer_GetLetterSpacing;
		b.TextRenderer_SetLetterSpacing = &Axiom_TextRenderer_SetLetterSpacing;
		b.TextRenderer_GetHAlign = &Axiom_TextRenderer_GetHAlign;
		b.TextRenderer_SetHAlign = &Axiom_TextRenderer_SetHAlign;
		b.TextRenderer_GetSortingOrder = &Axiom_TextRenderer_GetSortingOrder;
		b.TextRenderer_SetSortingOrder = &Axiom_TextRenderer_SetSortingOrder;
		b.TextRenderer_GetSortingLayer = &Axiom_TextRenderer_GetSortingLayer;
		b.TextRenderer_SetSortingLayer = &Axiom_TextRenderer_SetSortingLayer;

		b.Camera2D_GetOrthographicSize = &Axiom_Camera2D_GetOrthographicSize;
		b.Camera2D_SetOrthographicSize = &Axiom_Camera2D_SetOrthographicSize;
		b.Camera2D_GetZoom = &Axiom_Camera2D_GetZoom;
		b.Camera2D_SetZoom = &Axiom_Camera2D_SetZoom;
		b.Camera2D_GetClearColor = &Axiom_Camera2D_GetClearColor;
		b.Camera2D_SetClearColor = &Axiom_Camera2D_SetClearColor;
		b.Camera2D_ScreenToWorld = &Axiom_Camera2D_ScreenToWorld;
		b.Camera2D_GetViewportWidth = &Axiom_Camera2D_GetViewportWidth;
		b.Camera2D_GetViewportHeight = &Axiom_Camera2D_GetViewportHeight;
		b.Camera2D_GetMainEntity = &Axiom_Camera2D_GetMainEntity;

		b.Rigidbody2D_ApplyForce = &Axiom_Rigidbody2D_ApplyForce;
		b.Rigidbody2D_ApplyImpulse = &Axiom_Rigidbody2D_ApplyImpulse;
		b.Rigidbody2D_GetLinearVelocity = &Axiom_Rigidbody2D_GetLinearVelocity;
		b.Rigidbody2D_SetLinearVelocity = &Axiom_Rigidbody2D_SetLinearVelocity;
		b.Rigidbody2D_GetAngularVelocity = &Axiom_Rigidbody2D_GetAngularVelocity;
		b.Rigidbody2D_SetAngularVelocity = &Axiom_Rigidbody2D_SetAngularVelocity;
		b.Rigidbody2D_GetBodyType = &Axiom_Rigidbody2D_GetBodyType;
		b.Rigidbody2D_SetBodyType = &Axiom_Rigidbody2D_SetBodyType;
		b.Rigidbody2D_GetGravityScale = &Axiom_Rigidbody2D_GetGravityScale;
		b.Rigidbody2D_SetGravityScale = &Axiom_Rigidbody2D_SetGravityScale;
		b.Rigidbody2D_GetMass = &Axiom_Rigidbody2D_GetMass;
		b.Rigidbody2D_SetMass = &Axiom_Rigidbody2D_SetMass;

		b.BoxCollider2D_GetScale = &Axiom_BoxCollider2D_GetScale;
		b.BoxCollider2D_GetCenter = &Axiom_BoxCollider2D_GetCenter;
		b.BoxCollider2D_SetEnabled = &Axiom_BoxCollider2D_SetEnabled;

		b.AudioSource_Play = &Axiom_AudioSource_Play;
		b.AudioSource_Pause = &Axiom_AudioSource_Pause;
		b.AudioSource_Stop = &Axiom_AudioSource_Stop;
		b.AudioSource_Resume = &Axiom_AudioSource_Resume;
		b.AudioSource_GetVolume = &Axiom_AudioSource_GetVolume;
		b.AudioSource_SetVolume = &Axiom_AudioSource_SetVolume;
		b.AudioSource_GetPitch = &Axiom_AudioSource_GetPitch;
		b.AudioSource_SetPitch = &Axiom_AudioSource_SetPitch;
		b.AudioSource_GetLoop = &Axiom_AudioSource_GetLoop;
		b.AudioSource_SetLoop = &Axiom_AudioSource_SetLoop;
		b.AudioSource_IsPlaying = &Axiom_AudioSource_IsPlaying;
		b.AudioSource_IsPaused = &Axiom_AudioSource_IsPaused;
		b.AudioSource_GetAudio = &Axiom_AudioSource_GetAudio;
		b.AudioSource_SetAudio = &Axiom_AudioSource_SetAudio;

		b.FastBody2D_GetBodyType = &Axiom_FastBody2D_GetBodyType;
		b.FastBody2D_SetBodyType = &Axiom_FastBody2D_SetBodyType;
		b.FastBody2D_GetMass = &Axiom_FastBody2D_GetMass;
		b.FastBody2D_SetMass = &Axiom_FastBody2D_SetMass;
		b.FastBody2D_GetUseGravity = &Axiom_FastBody2D_GetUseGravity;
		b.FastBody2D_SetUseGravity = &Axiom_FastBody2D_SetUseGravity;
		b.FastBody2D_GetVelocity = &Axiom_FastBody2D_GetVelocity;
		b.FastBody2D_SetVelocity = &Axiom_FastBody2D_SetVelocity;
		b.FastBoxCollider2D_GetHalfExtents = &Axiom_FastBoxCollider2D_GetHalfExtents;
		b.FastBoxCollider2D_SetHalfExtents = &Axiom_FastBoxCollider2D_SetHalfExtents;
		b.FastCircleCollider2D_GetRadius = &Axiom_FastCircleCollider2D_GetRadius;
		b.FastCircleCollider2D_SetRadius = &Axiom_FastCircleCollider2D_SetRadius;

		b.Scene_GetActiveSceneName = &Axiom_Scene_GetActiveSceneName;
		b.Scene_GetActiveSceneNameBuffer = &Axiom_Scene_GetActiveSceneNameBuffer;
		b.Scene_GetEntityCount = &Axiom_Scene_GetEntityCount;
		b.Scene_GetEntityCountByName = &Axiom_Scene_GetEntityCountByName;
		b.Scene_LoadAdditive = &Axiom_Scene_LoadAdditive;
		b.Scene_Load = &Axiom_Scene_Load;
		b.Scene_Unload = &Axiom_Scene_Unload;
		b.Scene_SetActive = &Axiom_Scene_SetActive;
		b.Scene_Reload = &Axiom_Scene_Reload;
		b.Scene_SetGameSystemEnabled = &Axiom_Scene_SetGameSystemEnabled;
		b.Scene_SetGlobalSystemEnabled = &Axiom_Scene_SetGlobalSystemEnabled;
		b.Scene_DoesSceneExist = &Axiom_Scene_DoesSceneExist;
		b.Scene_GetLoadedCount = &Axiom_Scene_GetLoadedCount;
		b.Scene_GetLoadedSceneNameAt = &Axiom_Scene_GetLoadedSceneNameAt;
		b.Scene_GetLoadedSceneNameAtBuffer = &Axiom_Scene_GetLoadedSceneNameAtBuffer;
		b.Scene_GetEntityNameByUUID = &Axiom_Scene_GetEntityNameByUUID;
		b.Scene_GetEntityNameByUUIDBuffer = &Axiom_Scene_GetEntityNameByUUIDBuffer;
		b.Scene_QueryEntities = &Axiom_Scene_QueryEntities;
		b.Scene_QueryEntitiesFiltered = &Axiom_Scene_QueryEntitiesFiltered;
		b.Scene_QueryEntitiesInScene = &Axiom_Scene_QueryEntitiesInScene;
		b.Scene_QueryEntitiesFilteredInScene = &Axiom_Scene_QueryEntitiesFilteredInScene;

		b.Asset_IsValid = &Axiom_Asset_IsValid;
		b.Asset_GetOrCreateUUIDFromPath = &Axiom_Asset_GetOrCreateUUIDFromPath;
		b.Asset_GetPath = &Axiom_Asset_GetPath;
		b.Asset_GetPathBuffer = &Axiom_Asset_GetPathBuffer;
		b.Asset_GetDisplayName = &Axiom_Asset_GetDisplayName;
		b.Asset_GetDisplayNameBuffer = &Axiom_Asset_GetDisplayNameBuffer;
		b.Asset_GetKind = &Axiom_Asset_GetKind;
		b.Asset_FindAll = &Axiom_Asset_FindAll;
		b.Asset_FindAllBuffer = &Axiom_Asset_FindAllBuffer;
		b.Texture_LoadAsset = &Axiom_Texture_LoadAsset;
		b.Texture_GetWidth = &Axiom_Texture_GetWidth;
		b.Texture_GetHeight = &Axiom_Texture_GetHeight;
		b.Audio_LoadAsset = &Axiom_Audio_LoadAsset;
		b.Audio_PlayOneShotAsset = &Axiom_Audio_PlayOneShotAsset;
		b.Font_LoadAsset = &Axiom_Font_LoadAsset;

		b.ParticleSystem2D_Play = &Axiom_ParticleSystem2D_Play;
		b.ParticleSystem2D_Pause = &Axiom_ParticleSystem2D_Pause;
		b.ParticleSystem2D_Stop = &Axiom_ParticleSystem2D_Stop;
		b.ParticleSystem2D_IsPlaying = &Axiom_ParticleSystem2D_IsPlaying;
		b.ParticleSystem2D_GetPlayOnAwake = &Axiom_ParticleSystem2D_GetPlayOnAwake;
		b.ParticleSystem2D_SetPlayOnAwake = &Axiom_ParticleSystem2D_SetPlayOnAwake;
		b.ParticleSystem2D_GetColor = &Axiom_ParticleSystem2D_GetColor;
		b.ParticleSystem2D_SetColor = &Axiom_ParticleSystem2D_SetColor;
		b.ParticleSystem2D_GetLifeTime = &Axiom_ParticleSystem2D_GetLifeTime;
		b.ParticleSystem2D_SetLifeTime = &Axiom_ParticleSystem2D_SetLifeTime;
		b.ParticleSystem2D_GetSpeed = &Axiom_ParticleSystem2D_GetSpeed;
		b.ParticleSystem2D_SetSpeed = &Axiom_ParticleSystem2D_SetSpeed;
		b.ParticleSystem2D_GetScale = &Axiom_ParticleSystem2D_GetScale;
		b.ParticleSystem2D_SetScale = &Axiom_ParticleSystem2D_SetScale;
		b.ParticleSystem2D_GetEmitOverTime = &Axiom_ParticleSystem2D_GetEmitOverTime;
		b.ParticleSystem2D_SetEmitOverTime = &Axiom_ParticleSystem2D_SetEmitOverTime;
		b.ParticleSystem2D_Emit = &Axiom_ParticleSystem2D_Emit;

		b.Gizmo_DrawLine = &Axiom_Gizmo_DrawLine;
		b.Gizmo_DrawSquare = &Axiom_Gizmo_DrawSquare;
		b.Gizmo_DrawCircle = &Axiom_Gizmo_DrawCircle;
		b.Gizmo_SetColor = &Axiom_Gizmo_SetColor;
		b.Gizmo_GetColor = &Axiom_Gizmo_GetColor;
		b.Gizmo_GetLineWidth = &Axiom_Gizmo_GetLineWidth;
		b.Gizmo_SetLineWidth = &Axiom_Gizmo_SetLineWidth;

		b.Physics2D_Raycast = &Axiom_Physics2D_Raycast;
		b.Physics2D_OverlapCircle = &Axiom_Physics2D_OverlapCircle;
		b.Physics2D_OverlapBox = &Axiom_Physics2D_OverlapBox;
		b.Physics2D_OverlapPolygon = &Axiom_Physics2D_OverlapPolygon;
		b.Physics2D_OverlapCircleAll = &Axiom_Physics2D_OverlapCircleAll;
		b.Physics2D_OverlapBoxAll = &Axiom_Physics2D_OverlapBoxAll;
		b.Physics2D_OverlapPolygonAll = &Axiom_Physics2D_OverlapPolygonAll;
		b.Physics2D_ContainsPoint = &Axiom_Physics2D_ContainsPoint;
		b.Physics2D_ContainsPointAll = &Axiom_Physics2D_ContainsPointAll;
	}

} // namespace Axiom
