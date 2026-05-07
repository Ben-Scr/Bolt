#pragma once
#include "Scene/EntityHandle.hpp"
#include "Components/ComponentUtils.hpp"
#include "Components/General/EntityMetaDataComponent.hpp"
#include "Core/Export.hpp"

namespace Axiom {
	class Scene;

	class AXIOM_API Entity {
		friend class Scene;
		friend class EntityHelper;

	public:
		static Entity Create();

		static void Destroy(Entity entity);
		void Destroy();
		// Sentinel "no entity" value. Const so callers can't accidentally
		// reseat the global to a real entity handle and corrupt every
		// downstream null-check. Defined in Entity.cpp.
		static const Entity Null;


		template<typename TComponent, typename... Args>
			requires (!std::is_empty_v<TComponent>)
		TComponent& AddComponent(Args&&... args) {
			EnsureValid("Cannot add component to invalid entity");
			return  ComponentUtils::AddComponent<TComponent>(*m_Registry, m_EntityHandle, std::forward<Args>(args)...);
		}

		template<typename TTag>
			requires std::is_empty_v<TTag>
		void AddComponent() {
			EnsureValid("Cannot add tag component to invalid entity");
			ComponentUtils::AddComponent<TTag>(*m_Registry, m_EntityHandle);
		}

		template<typename TComponent>
		bool HasComponent() const {
			if (!IsValid()) {
				return false;
			}
			return  ComponentUtils::HasComponent<TComponent>(*m_Registry, m_EntityHandle);
		}

		template<typename... TComponent>
		bool HasAnyComponent() const {
			if (!IsValid()) {
				return false;
			}
			return  ComponentUtils::HasAnyComponent<TComponent...>(*m_Registry, m_EntityHandle);
		}

		template<typename TComponent>
		TComponent& GetComponent() {
			EnsureValid("Cannot get component from invalid entity");
			return  ComponentUtils::GetComponent<TComponent>(*m_Registry, m_EntityHandle);
		}

		template<typename TComponent>
		const TComponent& GetComponent() const {
			EnsureValid("Cannot get component from invalid entity");
			return  ComponentUtils::GetComponent<TComponent>(*m_Registry, m_EntityHandle);
		}

		template<typename TComponent>
		bool TryGetComponent(TComponent*& out) {
			if (!IsValid()) {
				out = nullptr;
				return false;
			}
			out = ComponentUtils::TryGetComponent<TComponent>(*m_Registry, m_EntityHandle);
			return out != nullptr;
		}

		template<typename TComponent>
		void RemoveComponent() {
			if (!IsValid()) {
				return;
			}
			ComponentUtils::RemoveComponent<TComponent>(*m_Registry, m_EntityHandle);
		}

		std::string GetName() const;
		const EntityMetaData* GetMetaData() const;
		EntityOrigin GetOrigin() const;
		EntityID GetRuntimeID() const;
		AssetGUID GetSceneGUID() const;
		AssetGUID GetPrefabGUID() const;
		bool IsSceneEntity() const;
		bool IsPrefabInstance() const;
		bool IsRuntime() const;

		EntityHandle GetHandle() const;
		Scene* GetScene() { return m_Scene; }
		const Scene* GetScene() const { return m_Scene; }
		bool IsValid() const { return m_Registry && m_EntityHandle != entt::null && m_Registry->valid(m_EntityHandle); }

		void SetStatic(bool isStatic);
		void SetEnabled(bool enabled);

		// ── Parent-child hierarchy ────────────────────────────────────
		// Reparent this entity. Pass Entity::Null to detach (make this a
		// root). Detaches from the previous parent's child list first.
		// Refuses cycles (passing a descendant of `this`) silently — the
		// call becomes a no-op so a buggy editor drag-drop can't corrupt
		// the scene graph.
		void SetParent(Entity parent);

		// Returns Entity::Null when this is a root.
		Entity GetParent() const;

		// Direct children only. Modifying the returned vector through the
		// hierarchy component is unsafe — use SetParent for structural
		// edits so the parent's child list stays in sync.
		const std::vector<EntityHandle>& GetChildren() const;

		bool HasParent() const;
		bool IsAncestorOf(Entity other) const;

	private:
		void EnsureValid(const char* message) const;

		explicit Entity(EntityHandle e, Scene& scene);
		explicit Entity(EntityHandle e, Scene* scene);
		EntityHandle    m_EntityHandle;
		entt::registry* m_Registry;
		Scene*          m_Scene;
	};

	inline bool operator==(const Entity& a, const Entity& b) { return a.GetHandle() == b.GetHandle(); }
	inline bool operator!=(const Entity& a, const Entity& b) { return !(a == b); }
}
