#pragma once
#include "Collections/Vec2.hpp"
#include "Core/Export.hpp"
#include "Physics/AxiomContact2D.hpp"
#include "Scene/EntityHandle.hpp"

#include <PhysicsWorld.hpp>
#include <WorldSettings.hpp>
#include <Contact.hpp>
#include <BoxCollider.hpp>
#include <CircleCollider.hpp>

#include <unordered_map>
#include <functional>
#include <vector>

namespace Axiom {
	using AxiomContactCallback = std::function<void(const AxiomContact2D&)>;

	/// Engine-level wrapper around the Axiom-Physics PhysicsWorld.
	/// Manages Body/Collider ownership and provides per-entity contact callbacks.
	class AXIOM_API AxiomPhysicsWorld2D {
	public:
		AxiomPhysicsWorld2D();
		explicit AxiomPhysicsWorld2D(const AxiomPhys::WorldSettings& settings);

		void Step(float dt);
		void Destroy();

		AxiomPhys::PhysicsWorld& GetWorld() { return m_World; }
		const AxiomPhys::PhysicsWorld& GetWorld() const { return m_World; }

		void SetSettings(const AxiomPhys::WorldSettings& settings) { m_World.SetSettings(settings); }
		const AxiomPhys::WorldSettings& GetSettings() const { return m_World.GetSettings(); }

		// Body registration tied to an entity
		AxiomPhys::Body* CreateBody(EntityHandle entity, AxiomPhys::BodyType type);
		void DestroyBody(EntityHandle entity);
		AxiomPhys::Body* GetBody(EntityHandle entity);

		// Collider attachment
		AxiomPhys::BoxCollider* CreateBoxCollider(EntityHandle entity, const Vec2& halfExtents);
		AxiomPhys::CircleCollider* CreateCircleCollider(EntityHandle entity, float radius);
		void DestroyCollider(EntityHandle entity);

		// Contact callbacks per entity
		void RegisterContactCallback(EntityHandle entity, AxiomContactCallback callback);
		void UnregisterContactCallback(EntityHandle entity);

		size_t GetBodyCount() const { return m_World.GetBodyCount(); }

	private:
		void DispatchContacts();

		AxiomPhys::PhysicsWorld m_World;

		// Ownership: bodies and colliders keyed by entity
		std::unordered_map<uint32_t, std::unique_ptr<AxiomPhys::Body>> m_Bodies;
		std::unordered_map<uint32_t, std::unique_ptr<AxiomPhys::Collider>> m_Colliders;

		// Entity lookup from Body pointer (reverse map)
		std::unordered_map<AxiomPhys::Body*, EntityHandle> m_BodyToEntity;

		// Contact callbacks per entity
		std::unordered_map<uint32_t, AxiomContactCallback> m_ContactCallbacks;
	};

}
