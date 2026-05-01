#include "pch.hpp"
#include "Scene/Entity.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/Scene.hpp"
#include "Components/General/NameComponent.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Log.hpp"


namespace Axiom {
	Entity Entity::Null{ entt::null, static_cast<Scene*>(nullptr) };

	EntityHandle Entity::GetHandle() const {
		return m_EntityHandle;
	}

	Entity::Entity(EntityHandle e, Scene& scene)
		: Entity(e, &scene) {}

	Entity::Entity(EntityHandle e, Scene* scene)
		: m_EntityHandle(e), m_Registry(scene ? &scene->GetRegistry() : nullptr), m_Scene(scene) {}

	Entity Entity::Create() {
		Scene* activeScene = SceneManager::Get().GetActiveScene();
		if (!activeScene || !activeScene->IsLoaded()) {
			AIM_ERROR_TAG("Entity", "Cannot create entity: no active scene loaded");
			return Entity::Null;
		}

		return activeScene->CreateRuntimeEntity();
	}

	void Entity::Destroy(Entity entity) {
		entity.Destroy();
	}

	void Entity::Destroy() {
		if (!m_Registry || !m_Scene || m_EntityHandle == entt::null) {
			return;
		}

		const EntityHandle entityHandle = m_EntityHandle;
		Scene* owningScene = m_Scene;

		m_EntityHandle = entt::null;
		m_Registry = nullptr;
		m_Scene = nullptr;

		if (owningScene->IsValid(entityHandle)) {
			owningScene->DestroyEntity(entityHandle);
		}
	}

	std::string Entity::GetName() const {
		if (HasComponent<NameComponent>()) {
			return GetComponent<NameComponent>().Name;
		}
		else {
			return "Unnamed Entity (" + std::to_string(static_cast<std::uint32_t>(m_EntityHandle)) + ")";
		}
	}

	const EntityMetaData* Entity::GetMetaData() const {
		return m_Scene ? m_Scene->GetEntityMetaData(m_EntityHandle) : nullptr;
	}

	EntityOrigin Entity::GetOrigin() const {
		return m_Scene ? m_Scene->GetEntityOrigin(m_EntityHandle) : EntityOrigin::Runtime;
	}

	EntityID Entity::GetRuntimeID() const {
		return m_Scene ? m_Scene->GetRuntimeID(m_EntityHandle) : 0;
	}

	AssetGUID Entity::GetSceneGUID() const {
		return m_Scene ? m_Scene->GetSceneEntityGUID(m_EntityHandle) : AssetGUID(0);
	}

	AssetGUID Entity::GetPrefabGUID() const {
		return m_Scene ? m_Scene->GetPrefabGUID(m_EntityHandle) : AssetGUID(0);
	}

	bool Entity::IsSceneEntity() const {
		return GetOrigin() == EntityOrigin::Scene;
	}

	bool Entity::IsPrefabInstance() const {
		return GetOrigin() == EntityOrigin::Prefab;
	}

	bool Entity::IsRuntime() const {
		return GetOrigin() == EntityOrigin::Runtime;
	}

	void Entity::SetStatic(bool isStatic) {
		if (isStatic) { if (!HasComponent<StaticTag>()) AddComponent<StaticTag>(); }
		else { if (HasComponent<StaticTag>()) RemoveComponent<StaticTag>(); }
	}

	void Entity::SetEnabled(bool enabled) {
		if (enabled) { if (HasComponent<DisabledTag>()) RemoveComponent<DisabledTag>(); }
		else { if (!HasComponent<DisabledTag>()) AddComponent<DisabledTag>(); }
	}
}
