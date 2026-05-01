#pragma once
#include "Scene/Entity.hpp"
#include "Scene/ISystem.hpp"
#include "Collections/Ids.hpp"
#include "Components/General/EntityMetaDataComponent.hpp"
#include "Core/Export.hpp"
#include "Core/UUID.hpp"
#include <functional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Axiom {
	class SceneDefinition;
	class Camera2DComponent;
	class AXIOM_API Scene {
		friend class SceneManager;
		friend class SceneDefinition;
		friend class Application;

	public:
		Scene(const Scene&) = delete;

		// Info: Creates an entity with a Transform2D component
		Entity CreateEntity();
		// Info: Creates an entity with a Transform2D and Name component
		Entity CreateEntity(const std::string& name);
		Entity CreateRuntimeEntity();
		Entity CreateRuntimeEntity(const std::string& name);
		Entity CreatePrefabInstance(AssetGUID prefabGuid, const std::string& name = "Entity");
		// Info: Creates an entity handle and applies scene invariants such as UUID + dirty tracking
		EntityHandle CreateEntityHandle(
			EntityOrigin origin = EntityOrigin::Scene,
			AssetGUID prefabGuid = AssetGUID(0),
			AssetGUID sceneGuid = AssetGUID(0),
			EntityID runtimeId = 0);

		Entity GetEntity(EntityHandle nativeEntity) { return Entity(nativeEntity, *this); }
		Entity GetEntity(EntityHandle nativeEntity) const { return Entity(nativeEntity, const_cast<Scene&>(*this)); }

		void DestroyEntity(Entity entity);
		void DestroyEntity(EntityHandle nativeEntity);
		void ClearEntities();

		bool IsValid(EntityHandle nativeEntity) const {
			return m_Registry.valid(nativeEntity);
		}
		template<typename TComponent, typename... Args>
			requires (!std::is_empty_v<TComponent>)
		TComponent& AddComponent(EntityHandle entity, Args&&... args) {
			return ComponentUtils::AddComponent<TComponent>(m_Registry, entity, std::forward<Args>(args)...);
		}

		template<typename TTag>
			requires std::is_empty_v<TTag>
		void AddComponent(EntityHandle entity) {
			ComponentUtils::AddComponent<TTag>(m_Registry, entity);
		}

		template<typename TComponent>
		bool HasComponent(EntityHandle entity) const {
			return ComponentUtils::HasComponent<TComponent>(m_Registry, entity);
		}

		template<typename... TComponent>
		bool HasAnyComponent(EntityHandle entity) const {
			return ComponentUtils::HasAnyComponent<TComponent...>(m_Registry, entity);
		}

		template<typename... TComponent, typename... TEntity>
		bool AnyHasComponent(TEntity... handles) const {
			return ComponentUtils::AnyHasComponent<TComponent...>(m_Registry, handles...);
		}

		template<typename TComponent>
		TComponent& GetComponent(EntityHandle entity) {
			return ComponentUtils::GetComponent<TComponent>(m_Registry, entity);
		}

		template<typename TComponent>
		const TComponent& GetComponent(EntityHandle entity) const {
			return ComponentUtils::GetComponent<TComponent>(m_Registry, entity);
		}

		template<typename TComponent>
		bool TryGetComponent(EntityHandle entity, TComponent*& out) {
			out = ComponentUtils::TryGetComponent<TComponent>(m_Registry, entity);
			return out != nullptr;
		}

		template<typename TComponent>
		void RemoveComponent(EntityHandle entity) {
			ComponentUtils::RemoveComponent<TComponent>(m_Registry, entity);
		}

		template<typename TComponent>
		TComponent* GetSingletonComponent() {
			auto view = m_Registry.view<TComponent>();
			const auto count = view.size();
			if (count == 0) {
				AIM_CORE_ERROR_TAG("Scene", "Singleton component '{}' not found", typeid(TComponent).name());
				return nullptr;
			}
			if (count > 1) {
				AIM_CORE_WARN_TAG("Scene", "Multiple ({}) instances of singleton component '{}', returning first", count, typeid(TComponent).name());
			}

			auto entity = *view.begin();
			return &view.get<TComponent>(entity);
		}

		template<typename TComponent>
		entt::entity GetSingletonEntity() {
			auto view = m_Registry.view<TComponent>();

			const auto count = view.size();
			if (count == 0) {
				AIM_CORE_ERROR_TAG("Scene", "Singleton entity with component '{}' not found", typeid(TComponent).name());
				return entt::null;
			}
			if (count > 1) {
				AIM_CORE_WARN_TAG("Scene", "Multiple ({}) instances of singleton component '{}', returning first", count, typeid(TComponent).name());
			}

			return *view.begin();
		}

		template<typename T>
		T* GetSystem() {
			static_assert(std::is_base_of<ISystem, T>::value, "T must derive from ISystem");

			for (auto& sysPtr : m_Systems) {
				if (auto ptr = dynamic_cast<T*>(sysPtr.get())) {
					return ptr;
				}
			}

			AIM_CORE_ERROR_TAG("Scene", "System not found: {}", typeid(T).name());
			return nullptr;
		}

		template<typename T>
		bool HasSystem() const {
			static_assert(std::is_base_of<ISystem, T>::value, "T must derive from ISystem");

			for (const auto& sysPtr : m_Systems) {
				if (dynamic_cast<T*>(sysPtr.get())) {
					return true;
				}
			}
			return false;
		}

		template<typename T>
		void DisableSystem() {
			static_assert(std::is_base_of<ISystem, T>::value, "T must derive from ISystem");

			for (auto& sysPtr : m_Systems) {
				if (auto ptr = dynamic_cast<T*>(sysPtr.get())) {
					if (ptr->IsEnabled()) {
						ptr->SetEnabled(false, *this);
					}
				}
			}
		}

		template<typename T>
		void EnableSystem() {
			static_assert(std::is_base_of<ISystem, T>::value, "T must derive from ISystem");

			for (auto& sysPtr : m_Systems) {
				if (auto ptr = dynamic_cast<T*>(sysPtr.get())) {
					ptr->SetEnabled(true, *this);
				}
			}
		}

		entt::registry& GetRegistry() { return m_Registry; }
		const entt::registry& GetRegistry() const { return m_Registry; }

		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& name) { m_Name = name; }
		bool IsLoaded() const { return m_IsLoaded; }
		bool IsPersistent() const { return m_Persistent; }
		const SceneDefinition* GetDefinition() const { return m_Definition; }
		Camera2DComponent* GetMainCamera();
		const Camera2DComponent* GetMainCamera() const;
		bool SetMainCamera(EntityHandle entity);

		bool IsDirty() const { return m_Dirty; }
		void MarkDirty();
		void ClearDirty() { m_Dirty = false; }

		const std::vector<std::string>& GetGameSystemClassNames() const { return m_GameSystemClassNames; }
		bool HasGameSystem(const std::string& className) const;
		bool AddGameSystem(const std::string& className);
		bool RemoveGameSystem(size_t index);
		bool MoveGameSystem(size_t fromIndex, size_t toIndex);
		void ClearGameSystems();
		bool SetGameSystemEnabled(const std::string& className, bool enabled);

		void SetEntityMetaData(
			EntityHandle entity,
			EntityOrigin origin,
			AssetGUID prefabGuid = AssetGUID(0),
			AssetGUID sceneGuid = AssetGUID(0),
			EntityID runtimeId = 0);
		EntityMetaData* GetEntityMetaData(EntityHandle entity);
		const EntityMetaData* GetEntityMetaData(EntityHandle entity) const;
		EntityOrigin GetEntityOrigin(EntityHandle entity) const;
		EntityID GetRuntimeID(EntityHandle entity) const;
		AssetGUID GetSceneEntityGUID(EntityHandle entity) const;
		AssetGUID GetPrefabGUID(EntityHandle entity) const;
		bool TryResolveRuntimeID(EntityID runtimeId, EntityHandle& outHandle) const;
		bool TryResolveSceneGUID(AssetGUID sceneGuid, EntityHandle& outHandle) const;

		UUID GetSceneId() const { return m_SceneId; }
		void SetSceneId(UUID id) { m_SceneId = id; }

	private:
		Scene(const std::string& name, const SceneDefinition* definition, bool IsPersistent);

		void OnTransform2DComponentConstruct(entt::registry& registry, EntityHandle entity);
		void OnRigidBody2DComponentConstruct(entt::registry& registry, EntityHandle entity);
		void OnRigidBody2DComponentDestroy(entt::registry& registry, EntityHandle entity);
		void OnBoxCollider2DComponentConstruct(entt::registry& registry, EntityHandle entity);
		void OnBoxCollider2DComponentDestroy(entt::registry& registry, EntityHandle entity);
		void OnAudioSourceComponentDestroy(entt::registry& registry, EntityHandle entity);
		void OnScriptComponentDestroy(entt::registry& registry, EntityHandle entity);

		void OnCamera2DComponentConstruct(entt::registry& registry, EntityHandle entity);
		void OnCamera2DComponentDestruct(entt::registry& registry, EntityHandle entity);
		void OnDisabledTagConstruct(entt::registry& registry, EntityHandle entity);
		void OnDisabledTagDestroy(entt::registry& registry, EntityHandle entity);

		void OnParticleSystem2DComponentConstruct(entt::registry& registry, EntityHandle entity);
		void OnParticleSystem2DComponentDestruct(entt::registry& registry, EntityHandle entity);

		void OnFastBody2DConstruct(entt::registry& registry, EntityHandle entity);
		void OnFastBody2DDestroy(entt::registry& registry, EntityHandle entity);
		void OnFastBoxCollider2DConstruct(entt::registry& registry, EntityHandle entity);
		void OnFastBoxCollider2DDestroy(entt::registry& registry, EntityHandle entity);
		void OnFastCircleCollider2DConstruct(entt::registry& registry, EntityHandle entity);
		void OnFastCircleCollider2DDestroy(entt::registry& registry, EntityHandle entity);
		void DestroyEntityInternal(EntityHandle nativeEntity, bool markDirty);
		EntityID AllocateRuntimeEntityID();
		void RegisterEntityIdentity(EntityHandle entity);
		void UnregisterEntityIdentity(EntityHandle entity);
		void TrackEntityDestruction(EntityHandle entity);
		void UntrackEntityDestruction(EntityHandle entity);
		bool IsEntityBeingDestroyed(EntityHandle entity) const;
		void ApplyEntityEnabledState(entt::registry& registry, EntityHandle entity, bool enabled);
		void RefreshMainCameraSelection(entt::registry& registry, EntityHandle preferred = entt::null, EntityHandle excluded = entt::null);

		void RunLifecycleSystems(const std::function<void(ISystem&)>& func, size_t* enteredSystemCount = nullptr);
		void AwakeSystems(size_t* enteredSystemCount = nullptr);
		void StartSystems(size_t* enteredSystemCount = nullptr);
		void UpdateSystems();
		void FixedUpdateSystems();
		void OnGuiSystems();
		void DestroyScene();
		void DestroyScene(size_t enabledSystemCount);
		void ForeachEnabledSystem(std::string_view phase, const std::function<void(ISystem&)>& func);

		entt::registry m_Registry;
		std::vector<std::unique_ptr<ISystem>> m_Systems;
		std::vector<std::string> m_GameSystemClassNames;


		std::string m_Name;
		const SceneDefinition* m_Definition;
		UUID m_SceneId;
		EntityHandle m_MainCameraEntity = entt::null;

		bool m_IsLoaded = false;
		bool m_Persistent = false;
		bool m_Dirty = false;
		EntityID m_NextRuntimeEntityID = 1;
		std::unordered_map<EntityID, EntityHandle> m_RuntimeIdToEntity;
		std::unordered_map<uint64_t, EntityHandle> m_SceneGuidToEntity;
		std::unordered_set<uint32_t> m_EntitiesBeingDestroyed;
	};
}
