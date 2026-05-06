#include "pch.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Entity.hpp"

#include "Components/Audio/AudioSourceComponent.hpp"

#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/FastBody2DComponent.hpp"
#include "Components/Physics/FastBoxCollider2DComponent.hpp"
#include "Components/Physics/FastCircleCollider2DComponent.hpp"
#include "Physics/PhysicsSystem2D.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/General/NameComponent.hpp"
#include "Graphics/StaticRenderData.hpp"
#include <Components/Tags.hpp>

#include "Physics/Box2DWorld.hpp"

#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/General/UUIDComponent.hpp"
#include "Components/General/EntityMetaDataComponent.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/PrefabInstanceComponent.hpp"
#include "Core/Application.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Serialization/SceneSerializer.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <typeinfo>
#include <utility>

namespace Axiom {
	namespace {
		class ManagedGameSystem final : public ISystem {
		public:
			explicit ManagedGameSystem(std::string className)
				: m_ClassName(std::move(className)) {}

			const std::string& GetClassName() const { return m_ClassName; }

			void Start(Scene& scene) override
			{
				if (m_Handle == 0) {
					if (!ScriptEngine::GameSystemClassExists(m_ClassName)) {
						AIM_CORE_WARN_TAG("Scene", "GameSystem '{}' was registered on scene '{}' but no matching class was found", m_ClassName, scene.GetName());
						return;
					}
					m_Handle = ScriptEngine::CreateGameSystemInstance(m_ClassName, scene.GetName());
				}

				if (m_Handle != 0) {
					if (!m_EnableInvoked) {
						ScriptEngine::InvokeGameSystemEnable(m_Handle);
						m_EnableInvoked = true;
					}
					ScriptEngine::InvokeGameSystemStart(m_Handle);
				}
			}

			void Update(Scene&) override
			{
				if (m_Handle != 0) {
					ScriptEngine::InvokeGameSystemUpdate(m_Handle);
				}
			}

			// Lazy-creates managed instance and guards against double-fire across reload.
			void Awake(Scene& scene) override
			{
				if (m_Handle == 0) {
					if (!ScriptEngine::GameSystemClassExists(m_ClassName)) {
						return; // Warn lives in Start so we don't double-log
					}
					m_Handle = ScriptEngine::CreateGameSystemInstance(m_ClassName, scene.GetName());
				}
				if (m_Handle != 0 && !m_AwakeInvoked) {
					ScriptEngine::InvokeGameSystemAwake(m_Handle);
					m_AwakeInvoked = true;
				}
			}

			void FixedUpdate(Scene&) override
			{
				if (m_Handle != 0) {
					ScriptEngine::InvokeGameSystemFixedUpdate(m_Handle);
				}
			}

			void OnEnable(Scene&) override
			{
				if (m_Handle != 0 && !m_EnableInvoked) {
					ScriptEngine::InvokeGameSystemEnable(m_Handle);
					m_EnableInvoked = true;
				}
			}

			void OnDisable(Scene&) override
			{
				if (m_Handle != 0 && m_EnableInvoked) {
					ScriptEngine::InvokeGameSystemDisable(m_Handle);
					m_EnableInvoked = false;
				}
			}

			void OnDestroy(Scene&) override
			{
				if (m_Handle == 0) {
					return;
				}

				if (m_EnableInvoked) {
					ScriptEngine::InvokeGameSystemDisable(m_Handle);
					m_EnableInvoked = false;
				}
				ScriptEngine::InvokeGameSystemDestroy(m_Handle);
				ScriptEngine::DestroyGameSystemInstance(m_Handle);
				m_Handle = 0;
			}

		private:
			std::string m_ClassName;
			uint32_t m_Handle = 0;
			bool m_EnableInvoked = false;
			bool m_AwakeInvoked = false; // H7
		};

		bool IsManagedGameSystem(const std::unique_ptr<ISystem>& system)
		{
			return dynamic_cast<ManagedGameSystem*>(system.get()) != nullptr;
		}
	}

	Entity Scene::CreateEntity() {
		auto entityHandle = CreateEntityHandle(EntityOrigin::Scene);
		AddComponent<Transform2DComponent>(entityHandle);
		return Entity(entityHandle, *this);
	}
	Entity Scene::CreateEntity(const std::string& name) {
		auto entityHandle = CreateEntityHandle(EntityOrigin::Scene);
		AddComponent<Transform2DComponent>(entityHandle);
		AddComponent<NameComponent>(entityHandle, name);
		return Entity(entityHandle, *this);
	}

	Entity Scene::CreateRuntimeEntity() {
		auto entityHandle = CreateEntityHandle(EntityOrigin::Runtime);
		AddComponent<Transform2DComponent>(entityHandle);
		return Entity(entityHandle, *this);
	}

	Entity Scene::CreateRuntimeEntity(const std::string& name) {
		auto entityHandle = CreateEntityHandle(EntityOrigin::Runtime);
		AddComponent<Transform2DComponent>(entityHandle);
		AddComponent<NameComponent>(entityHandle, name);
		return Entity(entityHandle, *this);
	}

	Entity Scene::CreatePrefabInstance(AssetGUID prefabGuid, const std::string& name) {
		// Instantiate through the serializer so prefab component data, identity, and overrides follow one path.
		const EntityHandle entityHandle = SceneSerializer::InstantiatePrefab(*this, static_cast<uint64_t>(prefabGuid));
		if (entityHandle == entt::null) {
			AIM_CORE_WARN_TAG("Scene", "Failed to instantiate prefab '{}'", static_cast<uint64_t>(prefabGuid));
			return Entity::Null;
		}

		if (!name.empty()) {
			if (HasComponent<NameComponent>(entityHandle)) {
				GetComponent<NameComponent>(entityHandle).Name = name;
			}
			else {
				AddComponent<NameComponent>(entityHandle, name);
			}
		}
		return Entity(entityHandle, *this);
	}

	EntityHandle Scene::CreateEntityHandle(EntityOrigin origin, AssetGUID prefabGuid, AssetGUID sceneGuid, EntityID runtimeId) {
		EntityHandle entityHandle = m_Registry.create();
		SetEntityMetaData(entityHandle, origin, prefabGuid, sceneGuid, runtimeId);
		if (origin != EntityOrigin::Runtime && !Application::GetIsPlaying()) {
			m_Dirty = true;
		}
		return entityHandle;
	}

	EntityID Scene::AllocateRuntimeEntityID() {
		while (m_NextRuntimeEntityID == 0 || m_RuntimeIdToEntity.contains(m_NextRuntimeEntityID)) {
			++m_NextRuntimeEntityID;
		}

		return m_NextRuntimeEntityID++;
	}

	void Scene::RegisterEntityIdentity(EntityHandle entity) {
		if (!m_Registry.valid(entity) || !m_Registry.all_of<EntityMetaDataComponent>(entity)) {
			return;
		}

		const EntityMetaData& metaData = m_Registry.get<EntityMetaDataComponent>(entity).MetaData;
		if (metaData.RuntimeID != 0) {
			m_RuntimeIdToEntity[metaData.RuntimeID] = entity;
		}
		if (metaData.Origin == EntityOrigin::Scene && static_cast<uint64_t>(metaData.SceneGUID) != 0) {
			m_SceneGuidToEntity[static_cast<uint64_t>(metaData.SceneGUID)] = entity;
		}
	}

	void Scene::UnregisterEntityIdentity(EntityHandle entity) {
		if (!m_Registry.valid(entity) || !m_Registry.all_of<EntityMetaDataComponent>(entity)) {
			return;
		}

		const EntityMetaData& metaData = m_Registry.get<EntityMetaDataComponent>(entity).MetaData;
		if (metaData.RuntimeID != 0) {
			auto runtimeIt = m_RuntimeIdToEntity.find(metaData.RuntimeID);
			if (runtimeIt != m_RuntimeIdToEntity.end() && runtimeIt->second == entity) {
				m_RuntimeIdToEntity.erase(runtimeIt);
			}
		}
		if (static_cast<uint64_t>(metaData.SceneGUID) != 0) {
			auto sceneIt = m_SceneGuidToEntity.find(static_cast<uint64_t>(metaData.SceneGUID));
			if (sceneIt != m_SceneGuidToEntity.end() && sceneIt->second == entity) {
				m_SceneGuidToEntity.erase(sceneIt);
			}
		}
	}

	void Scene::SetEntityMetaData(
		EntityHandle entity,
		EntityOrigin origin,
		AssetGUID prefabGuid,
		AssetGUID sceneGuid,
		EntityID runtimeId) {
		if (entity == entt::null || !m_Registry.valid(entity)) {
			return;
		}

		UnregisterEntityIdentity(entity);

		EntityMetaData metaData;
		if (m_Registry.all_of<EntityMetaDataComponent>(entity)) {
			metaData = m_Registry.get<EntityMetaDataComponent>(entity).MetaData;
		}

		metaData.RuntimeID = runtimeId != 0 ? runtimeId : (metaData.RuntimeID != 0 ? metaData.RuntimeID : AllocateRuntimeEntityID());
		metaData.Origin = origin;
		metaData.PrefabGUID = origin == EntityOrigin::Prefab ? prefabGuid : AssetGUID(0);
		metaData.SceneGUID = origin == EntityOrigin::Scene
			? (static_cast<uint64_t>(sceneGuid) != 0 ? sceneGuid : AssetGUID(UUID()))
			: AssetGUID(0);

		m_Registry.emplace_or_replace<EntityMetaDataComponent>(entity, EntityMetaDataComponent{ metaData });

		UUID legacyId = UUID(metaData.RuntimeID);
		if (metaData.Origin == EntityOrigin::Scene && static_cast<uint64_t>(metaData.SceneGUID) != 0) {
			legacyId = metaData.SceneGUID;
		}
		m_Registry.emplace_or_replace<UUIDComponent>(entity, legacyId);

		if (metaData.Origin == EntityOrigin::Prefab && static_cast<uint64_t>(metaData.PrefabGUID) != 0) {
			m_Registry.emplace_or_replace<PrefabInstanceComponent>(entity, PrefabInstanceComponent{ metaData.PrefabGUID });
		}
		else if (m_Registry.all_of<PrefabInstanceComponent>(entity)) {
			m_Registry.remove<PrefabInstanceComponent>(entity);
		}

		RegisterEntityIdentity(entity);
		if (origin != EntityOrigin::Runtime && !Application::GetIsPlaying()) {
			m_Dirty = true;
		}
	}

	EntityMetaData* Scene::GetEntityMetaData(EntityHandle entity) {
		if (entity == entt::null || !m_Registry.valid(entity) || !m_Registry.all_of<EntityMetaDataComponent>(entity)) {
			return nullptr;
		}

		return &m_Registry.get<EntityMetaDataComponent>(entity).MetaData;
	}

	const EntityMetaData* Scene::GetEntityMetaData(EntityHandle entity) const {
		if (entity == entt::null || !m_Registry.valid(entity) || !m_Registry.all_of<EntityMetaDataComponent>(entity)) {
			return nullptr;
		}

		return &m_Registry.get<EntityMetaDataComponent>(entity).MetaData;
	}

	EntityOrigin Scene::GetEntityOrigin(EntityHandle entity) const {
		const EntityMetaData* metaData = GetEntityMetaData(entity);
		return metaData ? metaData->Origin : EntityOrigin::Runtime;
	}

	EntityID Scene::GetRuntimeID(EntityHandle entity) const {
		const EntityMetaData* metaData = GetEntityMetaData(entity);
		return metaData ? metaData->RuntimeID : 0;
	}

	AssetGUID Scene::GetSceneEntityGUID(EntityHandle entity) const {
		const EntityMetaData* metaData = GetEntityMetaData(entity);
		return metaData ? metaData->SceneGUID : AssetGUID(0);
	}

	AssetGUID Scene::GetPrefabGUID(EntityHandle entity) const {
		const EntityMetaData* metaData = GetEntityMetaData(entity);
		return metaData ? metaData->PrefabGUID : AssetGUID(0);
	}

	bool Scene::TryResolveRuntimeID(EntityID runtimeId, EntityHandle& outHandle) const {
		outHandle = entt::null;
		if (runtimeId == 0) {
			return false;
		}

		const auto it = m_RuntimeIdToEntity.find(runtimeId);
		if (it == m_RuntimeIdToEntity.end() || !IsValid(it->second)) {
			return false;
		}

		outHandle = it->second;
		return true;
	}

	bool Scene::TryResolveSceneGUID(AssetGUID sceneGuid, EntityHandle& outHandle) const {
		outHandle = entt::null;
		const uint64_t guidValue = static_cast<uint64_t>(sceneGuid);
		if (guidValue == 0) {
			return false;
		}

		const auto it = m_SceneGuidToEntity.find(guidValue);
		if (it == m_SceneGuidToEntity.end() || !IsValid(it->second)) {
			return false;
		}

		outHandle = it->second;
		return true;
	}

	void Scene::DestroyEntity(Entity entity) { DestroyEntity(entity.GetHandle()); }
	void Scene::DestroyEntity(EntityHandle nativeEntity) { DestroyEntityInternal(nativeEntity, true); }

	void Scene::ClearEntities() {
		auto view = m_Registry.view<entt::entity>();
		std::vector<EntityHandle> entities;
		entities.reserve(view.size());

		for (EntityHandle entity : view) {
			entities.push_back(entity);
		}

		for (EntityHandle entity : entities) {
			if (m_Registry.valid(entity)) {
				DestroyEntityInternal(entity, false);
			}
		}
	}

	void Scene::MarkDirty() { if (!Application::GetIsPlaying()) m_Dirty = true; }

	bool Scene::HasGameSystem(const std::string& className) const
	{
		return std::find(m_GameSystemClassNames.begin(), m_GameSystemClassNames.end(), className) != m_GameSystemClassNames.end();
	}

	bool Scene::AddGameSystem(const std::string& className)
	{
		if (className.empty() || HasGameSystem(className)) {
			return false;
		}

		m_GameSystemClassNames.push_back(className);
		m_Systems.push_back(std::make_unique<ManagedGameSystem>(className));
		MarkDirty();
		return true;
	}

	bool Scene::RemoveGameSystem(size_t index)
	{
		if (index >= m_GameSystemClassNames.size()) {
			return false;
		}

		const std::string className = m_GameSystemClassNames[index];
		for (auto it = m_Systems.begin(); it != m_Systems.end(); ++it) {
			if (auto* managedSystem = dynamic_cast<ManagedGameSystem*>(it->get());
				managedSystem && managedSystem->GetClassName() == className) {
				if (m_IsLoaded) {
					// Symmetric teardown: disable before destroy, matching the
					// SetEnabled / OnEnable / OnDisable contract on ISystem.
					managedSystem->OnDisable(*this);
					managedSystem->OnDestroy(*this);
				}
				m_AwakenedSystems.erase(std::remove(m_AwakenedSystems.begin(), m_AwakenedSystems.end(), managedSystem), m_AwakenedSystems.end());
				m_Systems.erase(it);
				break;
			}
		}

		m_GameSystemClassNames.erase(m_GameSystemClassNames.begin() + static_cast<std::ptrdiff_t>(index));
		MarkDirty();
		return true;
	}

	bool Scene::MoveGameSystem(size_t fromIndex, size_t toIndex)
	{
		if (fromIndex >= m_GameSystemClassNames.size() || toIndex >= m_GameSystemClassNames.size() || fromIndex == toIndex) {
			return false;
		}

		// Reorder the names list.
		const std::string movedClassName = m_GameSystemClassNames[fromIndex];
		m_GameSystemClassNames.erase(m_GameSystemClassNames.begin() + static_cast<std::ptrdiff_t>(fromIndex));
		m_GameSystemClassNames.insert(m_GameSystemClassNames.begin() + static_cast<std::ptrdiff_t>(toIndex), movedClassName);

		// Reorder the m_Systems entries that correspond to managed systems
		// to match the new name order, WITHOUT calling OnDisable/OnDestroy
		// on the way out and OnAwake/OnEnable on the way back in. Native
		// (non-managed) systems keep their slot positions so a simple
		// reorder doesn't disturb the rest of the system list. Reordering
		// is a UX-level concern; user OnDestroy/OnAwake callbacks should
		// not see a phantom destroy+recreate just because someone dragged
		// a system in the inspector.
		std::vector<std::unique_ptr<ISystem>> managedExtracted;
		std::vector<size_t> managedSlots;
		managedExtracted.reserve(m_GameSystemClassNames.size());
		managedSlots.reserve(m_GameSystemClassNames.size());

		for (size_t i = 0; i < m_Systems.size(); ++i) {
			if (IsManagedGameSystem(m_Systems[i])) {
				managedExtracted.push_back(std::move(m_Systems[i]));
				managedSlots.push_back(i);
			}
		}

		// Build the reordered managed list following the names order. A name
		// without a matching extracted system (shouldn't happen in practice,
		// since AddGameSystem is the only path that grows the list) gets a
		// fresh ManagedGameSystem so the lists stay in lock-step (L5).
		std::vector<std::unique_ptr<ISystem>> managedReordered;
		managedReordered.reserve(m_GameSystemClassNames.size());
		for (const std::string& className : m_GameSystemClassNames) {
			bool placed = false;
			for (auto it = managedExtracted.begin(); it != managedExtracted.end(); ++it) {
				if (!*it) continue;
				auto* mg = static_cast<ManagedGameSystem*>(it->get());
				if (mg->GetClassName() == className) {
					managedReordered.push_back(std::move(*it));
					placed = true;
					break;
				}
			}
			if (!placed) {
				managedReordered.push_back(std::make_unique<ManagedGameSystem>(className));
			}
		}

		// Slot reordered managed systems back into the original managed
		// positions. Surplus extracted systems (a name was dropped) are
		// discarded — that path also calls OnDisable/OnDestroy so the
		// user observes a clean shutdown for the dropped system.
		for (size_t i = 0; i < managedSlots.size() && i < managedReordered.size(); ++i) {
			m_Systems[managedSlots[i]] = std::move(managedReordered[i]);
		}
		// If managedReordered grew beyond the original slot count (a new
		// system was injected), append the extras at the back.
		for (size_t i = managedSlots.size(); i < managedReordered.size(); ++i) {
			m_Systems.push_back(std::move(managedReordered[i]));
		}

		MarkDirty();
		return true;
	}

	void Scene::ClearGameSystems()
	{
		for (auto it = m_Systems.begin(); it != m_Systems.end(); ) {
			if (IsManagedGameSystem(*it)) {
				ISystem* system = it->get();
				if (m_IsLoaded) {
					system->OnDisable(*this);
					system->OnDestroy(*this);
				}
				m_AwakenedSystems.erase(std::remove(m_AwakenedSystems.begin(), m_AwakenedSystems.end(), system), m_AwakenedSystems.end());
				it = m_Systems.erase(it);
			}
			else {
				++it;
			}
		}
		m_GameSystemClassNames.clear();
	}

	bool Scene::SetGameSystemEnabled(const std::string& className, bool enabled)
	{
		if (className.empty()) {
			return false;
		}

		for (auto& system : m_Systems) {
			auto* managedSystem = dynamic_cast<ManagedGameSystem*>(system.get());
			if (!managedSystem || managedSystem->GetClassName() != className) {
				continue;
			}

			managedSystem->SetEnabled(enabled, *this);
			return true;
		}

		return false;
	}

	Camera2DComponent* Scene::GetMainCamera() {
		const auto isUsableCamera = [this](EntityHandle entity) {
			return entity != entt::null
				&& m_Registry.valid(entity)
				&& m_Registry.all_of<Camera2DComponent>(entity)
				&& !m_Registry.all_of<DisabledTag>(entity);
		};

		if (isUsableCamera(m_MainCameraEntity)) {
			return &m_Registry.get<Camera2DComponent>(m_MainCameraEntity);
		}

		auto view = m_Registry.view<Camera2DComponent>(entt::exclude<DisabledTag>);
		for (EntityHandle entity : view) {
			m_MainCameraEntity = entity;
			return &view.get<Camera2DComponent>(entity);
		}

		m_MainCameraEntity = entt::null;
		return nullptr;
	}

	const Camera2DComponent* Scene::GetMainCamera() const {
		// m_MainCameraEntity is `mutable` so this const overload can do
		// the same lazy-cache update without const_casting the registry.
		const auto isUsableCamera = [this](EntityHandle entity) {
			return entity != entt::null
				&& m_Registry.valid(entity)
				&& m_Registry.all_of<Camera2DComponent>(entity)
				&& !m_Registry.all_of<DisabledTag>(entity);
		};

		if (isUsableCamera(m_MainCameraEntity)) {
			return &m_Registry.get<Camera2DComponent>(m_MainCameraEntity);
		}

		auto view = m_Registry.view<Camera2DComponent>(entt::exclude<DisabledTag>);
		for (EntityHandle entity : view) {
			m_MainCameraEntity = entity;
			return &view.get<Camera2DComponent>(entity);
		}

		m_MainCameraEntity = entt::null;
		return nullptr;
	}

	EntityHandle Scene::GetMainCameraEntity() {
		// Side effect: refreshes m_MainCameraEntity if cached camera is stale.
		(void)GetMainCamera();
		return m_MainCameraEntity;
	}

	bool Scene::SetMainCamera(EntityHandle entity) {
		if (entity == entt::null
			|| !m_Registry.valid(entity)
			|| !m_Registry.all_of<Camera2DComponent>(entity)
			|| m_Registry.all_of<DisabledTag>(entity)) {
			return false;
		}

		m_MainCameraEntity = entity;
		return true;
	}

	void Scene::DestroyEntityInternal(EntityHandle nativeEntity, bool markDirty) {
		if (nativeEntity == entt::null || !m_Registry.valid(nativeEntity)) {
			return;
		}

		if (markDirty && !Application::GetIsPlaying()) {
			m_Dirty = true;
		}

		// Cascade-destroy any children, and unhook from the parent's
		// child list, before destroying this entity. Snapshotting the
		// children vector first so the inner destroy calls don't
		// invalidate the iterator we're recursing through.
		if (m_Registry.all_of<HierarchyComponent>(nativeEntity)) {
			HierarchyComponent& hc = m_Registry.get<HierarchyComponent>(nativeEntity);

			std::vector<EntityHandle> childrenSnapshot = hc.Children;
			for (EntityHandle child : childrenSnapshot) {
				DestroyEntityInternal(child, false);
			}

			const EntityHandle parent = hc.Parent;
			if (parent != entt::null
				&& m_Registry.valid(parent)
				&& m_Registry.all_of<HierarchyComponent>(parent))
			{
				auto& parentHc = m_Registry.get<HierarchyComponent>(parent);
				parentHc.Children.erase(
					std::remove(parentHc.Children.begin(), parentHc.Children.end(), nativeEntity),
					parentHc.Children.end());
			}
		}

		UnregisterEntityIdentity(nativeEntity);
		TrackEntityDestruction(nativeEntity);
		m_Registry.destroy(nativeEntity);
		UntrackEntityDestruction(nativeEntity);
	}

	void Scene::TrackEntityDestruction(EntityHandle entity) {
		m_EntitiesBeingDestroyed.insert(static_cast<uint32_t>(entity));
	}

	void Scene::UntrackEntityDestruction(EntityHandle entity) {
		m_EntitiesBeingDestroyed.erase(static_cast<uint32_t>(entity));
	}

	bool Scene::IsEntityBeingDestroyed(EntityHandle entity) const {
		return m_EntitiesBeingDestroyed.contains(static_cast<uint32_t>(entity));
	}

	void Scene::RunLifecycleSystems(const std::function<void(ISystem&)>& func, size_t* enteredSystemCount) {
		for (const auto& systemPointer : m_Systems) {
			ISystem& system = *systemPointer;
			if (!system.IsEnabled()) {
				continue;
			}

			if (enteredSystemCount) {
				++(*enteredSystemCount);
			}

			func(system);
		}
	}

	void Scene::AwakeSystems(size_t* enteredSystemCount) {
		m_AwakenedSystems.clear();
		for (const auto& systemPointer : m_Systems) {
			ISystem& system = *systemPointer;
			if (!system.IsEnabled()) {
				continue;
			}

			system.Awake(*this);
			m_AwakenedSystems.push_back(&system);
			if (enteredSystemCount) {
				++(*enteredSystemCount);
			}
		}
	}

	void Scene::StartSystems(size_t* enteredSystemCount) {
		RunLifecycleSystems([this](ISystem& s) { s.Start(*this); }, enteredSystemCount);
	}

	void Scene::UpdateSystems() {
		ForeachEnabledSystem("Update", [this](ISystem& s) { s.Update(*this); });
	}

	void Scene::FixedUpdateSystems() {
		ForeachEnabledSystem("FixedUpdate", [this](ISystem& s) { s.FixedUpdate(*this); });
	}

	void Scene::OnPreRenderSystems() {
		ForeachEnabledSystem("OnPreRender", [this](ISystem& s) { s.OnPreRender(*this); });
	}

	void Scene::ForeachEnabledSystem(std::string_view phase, const std::function<void(ISystem&)>& func) {
		for (size_t i = 0; i < m_Systems.size(); ++i) {
			ISystem& system = *m_Systems[i];
			if (!system.IsEnabled()) {
				continue;
			}

			try {
				func(system);
			}
			catch (const std::exception& e) {
				AIM_CORE_ERROR_TAG("Scene", "{} failed in '{}': {}", typeid(system).name(), phase, e.what());
				throw;
			}
			catch (...) {
				AIM_CORE_ERROR_TAG("Scene", "{} failed in '{}' with an unknown exception", typeid(system).name(), phase);
				throw;
			}
		}
	}

	void Scene::DestroyScene() {
		DestroyScene(std::numeric_limits<size_t>::max());
	}

	void Scene::DestroyScene(size_t enabledSystemCount) {
		std::vector<ISystem*> systemsToDestroy;
		systemsToDestroy.reserve(m_Systems.size());

		if (!m_AwakenedSystems.empty()) {
			const size_t count = std::min(enabledSystemCount, m_AwakenedSystems.size());
			systemsToDestroy.insert(systemsToDestroy.end(), m_AwakenedSystems.begin(), m_AwakenedSystems.begin() + static_cast<std::ptrdiff_t>(count));
		}
		else {
			for (const auto& systemPointer : m_Systems) {
				ISystem& system = *systemPointer;
				if (!system.IsEnabled()) {
					continue;
				}

				systemsToDestroy.push_back(&system);
				if (systemsToDestroy.size() >= enabledSystemCount) {
					break;
				}
			}
		}

		std::exception_ptr firstFailure;
		for (auto it = systemsToDestroy.rbegin(); it != systemsToDestroy.rend(); ++it) {
			try {
				(*it)->OnDestroy(*this);
			}
			catch (const std::exception& e) {
				AIM_CORE_ERROR_TAG("Scene", "System error during destroy: {}", e.what());
				if (!firstFailure) {
					firstFailure = std::current_exception();
				}
			}
			catch (...) {
				AIM_CORE_ERROR_TAG("Scene", "Unknown system error during destroy");
				if (!firstFailure) {
					firstFailure = std::current_exception();
				}
			}
		}

		m_AwakenedSystems.clear();

		if (firstFailure) {
			std::rethrow_exception(firstFailure);
		}
	}

	std::unique_ptr<Scene> Scene::CreateDetachedScene(const std::string& name) {
		auto scene = std::unique_ptr<Scene>(new Scene(name, nullptr, false));
		scene->m_IsDetached = true;
		return scene;
	}

	Scene::Scene(const std::string& name, const SceneDefinition* definition, bool IsPersistent)
		: m_Name(name)
		, m_Definition(definition)
		, m_Persistent(IsPersistent)
		, m_IsLoaded(false) {

		m_Registry.on_construct<Rigidbody2DComponent>().connect<&Scene::OnRigidBody2DComponentConstruct>(this);
		m_Registry.on_construct<BoxCollider2DComponent>().connect<&Scene::OnBoxCollider2DComponentConstruct>(this);
		m_Registry.on_construct<Camera2DComponent>().connect<&Scene::OnCamera2DComponentConstruct>(this);
		m_Registry.on_construct<ParticleSystem2DComponent>().connect<&Scene::OnParticleSystem2DComponentConstruct>(this);
		m_Registry.on_construct<DisabledTag>().connect<&Scene::OnDisabledTagConstruct>(this);

		m_Registry.on_destroy<Rigidbody2DComponent>().connect<&Scene::OnRigidBody2DComponentDestroy>(this);
		m_Registry.on_destroy<BoxCollider2DComponent>().connect<&Scene::OnBoxCollider2DComponentDestroy>(this);
		m_Registry.on_destroy<AudioSourceComponent>().connect<&Scene::OnAudioSourceComponentDestroy>(this);
		m_Registry.on_destroy<ScriptComponent>().connect<&Scene::OnScriptComponentDestroy>(this);
		m_Registry.on_destroy<Camera2DComponent>().connect<&Scene::OnCamera2DComponentDestruct>(this);
		m_Registry.on_destroy<ParticleSystem2DComponent>().connect<&Scene::OnParticleSystem2DComponentDestruct>(this);
		m_Registry.on_destroy<DisabledTag>().connect<&Scene::OnDisabledTagDestroy>(this);
		m_Registry.on_destroy<StaticTag>().connect<&Scene::OnStaticTagDestroy>(this);

		// Axiom-Physics component hooks
		m_Registry.on_construct<FastBody2DComponent>().connect<&Scene::OnFastBody2DConstruct>(this);
		m_Registry.on_destroy<FastBody2DComponent>().connect<&Scene::OnFastBody2DDestroy>(this);
		m_Registry.on_construct<FastBoxCollider2DComponent>().connect<&Scene::OnFastBoxCollider2DConstruct>(this);
		m_Registry.on_destroy<FastBoxCollider2DComponent>().connect<&Scene::OnFastBoxCollider2DDestroy>(this);
		m_Registry.on_construct<FastCircleCollider2DComponent>().connect<&Scene::OnFastCircleCollider2DConstruct>(this);
		m_Registry.on_destroy<FastCircleCollider2DComponent>().connect<&Scene::OnFastCircleCollider2DDestroy>(this);
	}

	void Scene::OnRigidBody2DComponentConstruct(entt::registry& registry, EntityHandle entity)
	{
		// Skip Box2D body creation for editor-preview scenes (e.g. prefab inspector
		// thumbnails). Without this gate, every preview entity would push a real body
		// into the singleton physics world and corrupt simulation state on the live scene.
		// Drawers must defend against rb2D.IsValid() == false rather than assume.
		if (m_IsDetached) return;

		if (!registry.all_of<Transform2DComponent>(entity)) {
			AIM_CORE_WARN_TAG("Scene", "Adding missing Transform2DComponent before creating Rigidbody2D for entity {}", static_cast<uint32_t>(entity));
			registry.emplace<Transform2DComponent>(entity);
		}

		bool isEnabled = !registry.all_of<DisabledTag>(entity);
		Rigidbody2DComponent& rb2D = registry.get<Rigidbody2DComponent>(entity);

		if (HasAnyComponent<BoxCollider2DComponent>(entity)) {
			rb2D.m_BodyId = GetComponent<BoxCollider2DComponent>(entity).m_BodyId;
			rb2D.SetBodyType(BodyType::Dynamic);
		}
		else {
			rb2D.m_BodyId = PhysicsSystem2D::GetMainPhysicsWorld().CreateBody(entity, *this, BodyType::Dynamic);
		}

		rb2D.SetEnabled(isEnabled);
	}

	void Scene::OnRigidBody2DComponentDestroy(entt::registry& registry, EntityHandle entity) {
		if (m_IsDetached) return;
		auto& rb2D = GetComponent<Rigidbody2DComponent>(entity);

		if (!rb2D.IsValid()) return;

		if (IsEntityBeingDestroyed(entity) && registry.all_of<BoxCollider2DComponent>(entity)) {
			registry.get<BoxCollider2DComponent>(entity).Destroy();
		}
		else if (registry.all_of<BoxCollider2DComponent>(entity)) {
			rb2D.SetBodyType(BodyType::Static);
		}
		else {
			rb2D.Destroy();
		}
	}

	void Scene::OnBoxCollider2DComponentConstruct(entt::registry& registry, EntityHandle entity) {
		if (m_IsDetached) return;
		if (!registry.all_of<Transform2DComponent>(entity)) {
			AIM_CORE_WARN_TAG("Scene", "Adding missing Transform2DComponent before creating BoxCollider2D for entity {}", static_cast<uint32_t>(entity));
			registry.emplace<Transform2DComponent>(entity);
		}

		bool isEnabled = !registry.all_of<DisabledTag>(entity);
		BoxCollider2DComponent& boxCollider = GetComponent<BoxCollider2DComponent>(entity);
		boxCollider.m_EntityHandle = entity;

		// Reuse existing rigidbody if present, else spawn a static body.
		if (HasComponent<Rigidbody2DComponent>(entity)) {
			auto& rb = GetComponent<Rigidbody2DComponent>(entity);
			boxCollider.m_BodyId = rb.GetBodyHandle();
		}
		else {
			boxCollider.m_BodyId = PhysicsSystem2D::GetMainPhysicsWorld().CreateBody(entity, *this, BodyType::Static);
		}

		boxCollider.m_ShapeId = PhysicsSystem2D::GetMainPhysicsWorld().CreateShape(entity, *this, boxCollider.m_BodyId, ShapeType::Square);
		boxCollider.SetEnabled(isEnabled);
	}

	void Scene::OnBoxCollider2DComponentDestroy(entt::registry& registry, EntityHandle entity) {
		if (m_IsDetached) return;
		auto& boxCollider2D = GetComponent<BoxCollider2DComponent>(entity);
		if (IsEntityBeingDestroyed(entity) || !registry.all_of<Rigidbody2DComponent>(entity)) {
			boxCollider2D.Destroy();
		}
		else {
			boxCollider2D.DestroyShape(false);
		}
	}

	void Scene::OnAudioSourceComponentDestroy(entt::registry& registry, EntityHandle entity)
	{
		if (m_IsDetached) return;
		registry.get<AudioSourceComponent>(entity).Destroy();
	}

	void Scene::OnScriptComponentDestroy(entt::registry& registry, EntityHandle entity)
	{
		if (m_IsDetached) return; // No script lifecycle ran in this scene.
		(void)registry;
		ScriptSystem::RemoveAllScripts(Entity(entity, *this));
	}

	void Scene::OnCamera2DComponentConstruct(entt::registry& registry, EntityHandle entity)
	{
		if (!registry.all_of<Transform2DComponent>(entity)) {
			AIM_CORE_WARN_TAG("Scene", "Adding missing Transform2DComponent before creating Camera2D for entity {}", static_cast<uint32_t>(entity));
			registry.emplace<Transform2DComponent>(entity);
		}

		Camera2DComponent& camera2D = GetComponent<Camera2DComponent>(entity);
		camera2D.Initialize(*this, entity);
		camera2D.UpdateViewport();

		if (!registry.all_of<DisabledTag>(entity)
			&& (m_MainCameraEntity == entt::null
				|| !registry.valid(m_MainCameraEntity)
				|| registry.all_of<DisabledTag>(m_MainCameraEntity))) {
			RefreshMainCameraSelection(registry, entity);
		}
	}

	void Scene::OnCamera2DComponentDestruct(entt::registry& registry, EntityHandle entity)
	{
		Camera2DComponent& camera2D = GetComponent<Camera2DComponent>(entity);
		camera2D.Destroy();

		if (m_MainCameraEntity == entity) {
			RefreshMainCameraSelection(registry, entt::null, entity);
		}
	}

	void Scene::OnDisabledTagConstruct(entt::registry& registry, EntityHandle entity)
	{
		if (IsEntityBeingDestroyed(entity)) {
			return;
		}

		ApplyEntityEnabledState(registry, entity, false);
	}

	void Scene::OnDisabledTagDestroy(entt::registry& registry, EntityHandle entity)
	{
		if (IsEntityBeingDestroyed(entity)) {
			return;
		}

		ApplyEntityEnabledState(registry, entity, true);
	}

	void Scene::OnStaticTagDestroy(entt::registry& registry, EntityHandle entity)
	{
		if (registry.all_of<StaticRenderData>(entity)) {
			registry.remove<StaticRenderData>(entity);
		}
	}

	void Scene::OnParticleSystem2DComponentConstruct(entt::registry& registry, EntityHandle entity) {
		auto& ps = registry.get<ParticleSystem2DComponent>(entity);
		ps.m_EmitterScene = this;
		ps.m_EmitterEntity = entity;
	}

	void Scene::OnParticleSystem2DComponentDestruct(entt::registry& registry, EntityHandle entity) {
		auto& ps = registry.get<ParticleSystem2DComponent>(entity);
		ps.m_EmitterScene = nullptr;
		ps.m_EmitterEntity = entt::null;
	}

	// ── Axiom-Physics component hooks ────────────────────────────────
	// All Fast* hooks gate on m_IsDetached the same way the Box2D
	// hooks above do — without it, prefab-inspector preview entities
	// would push real bodies/colliders into the singleton AxiomPhysicsWorld
	// and corrupt simulation state on the live scene.

	void Scene::OnFastBody2DConstruct(entt::registry& registry, EntityHandle entity) {
		if (m_IsDetached) return;
		auto& comp = registry.get<FastBody2DComponent>(entity);
		auto& axiomWorld = PhysicsSystem2D::GetAxiomPhysicsWorld();

		comp.m_Body = axiomWorld.CreateBody(entity, comp.Type);
		if (comp.m_Body) {
			comp.m_Body->SetMass(comp.Mass);
			comp.m_Body->SetGravityEnabled(comp.UseGravity);
			comp.m_Body->SetBoundaryCheckEnabled(comp.BoundaryCheck);

			// Sync initial position from Transform
			if (HasComponent<Transform2DComponent>(entity)) {
				auto& tf = GetComponent<Transform2DComponent>(entity);
				comp.m_Body->SetPosition({ tf.Position.x, tf.Position.y });
			}
		}
	}

	void Scene::OnFastBody2DDestroy(entt::registry& registry, EntityHandle entity) {
		if (m_IsDetached) return;
		PhysicsSystem2D::GetAxiomPhysicsWorld().DestroyBody(entity);
	}

	void Scene::OnFastBoxCollider2DConstruct(entt::registry& registry, EntityHandle entity) {
		if (m_IsDetached) return;
		auto& comp = registry.get<FastBoxCollider2DComponent>(entity);
		auto& axiomWorld = PhysicsSystem2D::GetAxiomPhysicsWorld();
		comp.m_Collider = axiomWorld.CreateBoxCollider(entity, comp.HalfExtents);
	}

	void Scene::OnFastBoxCollider2DDestroy(entt::registry& registry, EntityHandle entity) {
		if (m_IsDetached) return;
		PhysicsSystem2D::GetAxiomPhysicsWorld().DestroyCollider(entity, AxiomPhysicsWorld2D::FastColliderKind::Box);
	}

	void Scene::OnFastCircleCollider2DConstruct(entt::registry& registry, EntityHandle entity) {
		if (m_IsDetached) return;
		auto& comp = registry.get<FastCircleCollider2DComponent>(entity);
		auto& axiomWorld = PhysicsSystem2D::GetAxiomPhysicsWorld();
		comp.m_Collider = axiomWorld.CreateCircleCollider(entity, comp.Radius);
	}

	void Scene::OnFastCircleCollider2DDestroy(entt::registry& registry, EntityHandle entity) {
		if (m_IsDetached) return;
		PhysicsSystem2D::GetAxiomPhysicsWorld().DestroyCollider(entity, AxiomPhysicsWorld2D::FastColliderKind::Circle);
	}

	void Scene::ApplyEntityEnabledState(entt::registry& registry, EntityHandle entity, bool enabled)
	{
		if (!registry.valid(entity)) {
			return;
		}

		if (auto* rigidbody = registry.try_get<Rigidbody2DComponent>(entity); rigidbody && rigidbody->IsValid()) {
			rigidbody->SetEnabled(enabled);
		}

		if (auto* collider = registry.try_get<BoxCollider2DComponent>(entity); collider && collider->IsValid()) {
			collider->SetEnabled(enabled);
		}

		if (registry.all_of<ScriptComponent>(entity)) {
			ScriptSystem::SetScriptsEnabled(Entity(entity, *this), enabled);
		}

		if (!registry.all_of<Camera2DComponent>(entity)) {
			return;
		}

		if (!enabled && m_MainCameraEntity == entity) {
			RefreshMainCameraSelection(registry, entt::null, entity);
		}
		else if (enabled
			&& (m_MainCameraEntity == entt::null
				|| !registry.valid(m_MainCameraEntity)
				|| registry.all_of<DisabledTag>(m_MainCameraEntity))) {
			RefreshMainCameraSelection(registry, entity);
		}
	}

	void Scene::RefreshMainCameraSelection(entt::registry& registry, EntityHandle preferred, EntityHandle excluded)
	{
		const auto isUsableCamera = [&](EntityHandle entity) {
			return entity != entt::null
				&& entity != excluded
				&& registry.valid(entity)
				&& registry.all_of<Camera2DComponent>(entity)
				&& !registry.all_of<DisabledTag>(entity);
		};

		if (isUsableCamera(preferred)) {
			m_MainCameraEntity = preferred;
			return;
		}

		auto view = registry.view<Camera2DComponent>(entt::exclude<DisabledTag>);
		for (EntityHandle candidate : view) {
			if (candidate != excluded) {
				m_MainCameraEntity = candidate;
				return;
			}
		}

		m_MainCameraEntity = entt::null;
	}
}
