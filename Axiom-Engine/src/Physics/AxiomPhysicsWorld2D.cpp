#include "pch.hpp"
#include "Physics/AxiomPhysicsWorld2D.hpp"

#include <BoxCollider.hpp>
#include <CircleCollider.hpp>

namespace Axiom {

	AxiomPhysicsWorld2D::AxiomPhysicsWorld2D() : m_World() {}

	AxiomPhysicsWorld2D::AxiomPhysicsWorld2D(const AxiomPhys::WorldSettings& settings)
		: m_World(settings) {
	}

	void AxiomPhysicsWorld2D::Step(float dt) {
		m_World.Step(dt);
		DispatchContacts();
	}

	void AxiomPhysicsWorld2D::Destroy() {
		m_ContactCallbacks.clear();
		m_BodyToEntity.clear();
		m_AttachedColliderKind.clear();

		for (auto& [key, body] : m_Bodies) {
			m_World.DetachCollider(*body);
			m_World.UnregisterBody(*body);
		}
		for (auto& [key, collider] : m_Colliders) {
			(void)key;
			m_World.UnregisterCollider(*collider);
		}

		m_Bodies.clear();
		m_Colliders.clear();
	}

	AxiomPhys::Body* AxiomPhysicsWorld2D::CreateBody(EntityHandle entity, AxiomPhys::BodyType type) {
		uint32_t key = static_cast<uint32_t>(entity);
		if (m_Bodies.count(key)) {
			AIM_CORE_WARN_TAG("AxiomPhysics", "Entity already has a Axiom-Physics body");
			return m_Bodies[key].get();
		}

		auto body = std::make_unique<AxiomPhys::Body>(type);
		AxiomPhys::Body* ptr = body.get();
		m_World.RegisterBody(*ptr);
		m_BodyToEntity[ptr] = entity;
		m_Bodies[key] = std::move(body);
		return ptr;
	}

	void AxiomPhysicsWorld2D::DestroyBody(EntityHandle entity) {
		uint32_t key = static_cast<uint32_t>(entity);

		auto it = m_Bodies.find(key);
		if (it == m_Bodies.end()) return;

		AxiomPhys::Body* ptr = it->second.get();

		// Detach (but do NOT destroy) any attached collider. Collider
		// components own their own lifetime and clean up via their own
		// on_destroy hook; destroying their AxiomPhys::Collider here
		// would dangle the raw m_Collider pointer stored in the still-
		// alive component. After detach, the surviving collider is
		// registered-but-orphaned in the world until its component goes.
		m_World.DetachCollider(*ptr);
		m_AttachedColliderKind.erase(key);

		m_World.UnregisterBody(*ptr);
		m_BodyToEntity.erase(ptr);
		m_Bodies.erase(it);
		m_ContactCallbacks.erase(key);
	}

	AxiomPhys::Body* AxiomPhysicsWorld2D::GetBody(EntityHandle entity) {
		uint32_t key = static_cast<uint32_t>(entity);
		auto it = m_Bodies.find(key);
		return it != m_Bodies.end() ? it->second.get() : nullptr;
	}

	AxiomPhys::BoxCollider* AxiomPhysicsWorld2D::CreateBoxCollider(EntityHandle entity, const Vec2& halfExtents) {
		uint32_t key = static_cast<uint32_t>(entity);

		// Replace only the BOX collider — keeps a coexisting circle collider's pointer valid.
		DestroyCollider(entity, FastColliderKind::Box);

		auto collider = std::make_unique<AxiomPhys::BoxCollider>(AxiomPhys::Vec2(halfExtents.x, halfExtents.y));
		AxiomPhys::BoxCollider* ptr = collider.get();
		m_World.RegisterCollider(*ptr);

		// Only attach to the body if nothing else is currently attached.
		// Inspector blocks user-driven dual colliders via DeclareConflict,
		// but deserialization and scripting can still produce both kinds
		// on the same entity. In that case the second creation just
		// registers in the world without attaching, so the previously
		// attached collider keeps its body-side state.
		auto bodyIt = m_Bodies.find(key);
		if (bodyIt != m_Bodies.end()
			&& m_AttachedColliderKind.find(key) == m_AttachedColliderKind.end()) {
			m_World.AttachCollider(*bodyIt->second, *ptr);
			m_AttachedColliderKind[key] = FastColliderKind::Box;
		}

		m_Colliders[ColliderKey{ key, FastColliderKind::Box }] = std::move(collider);
		return ptr;
	}

	AxiomPhys::CircleCollider* AxiomPhysicsWorld2D::CreateCircleCollider(EntityHandle entity, float radius) {
		uint32_t key = static_cast<uint32_t>(entity);

		DestroyCollider(entity, FastColliderKind::Circle);

		auto collider = std::make_unique<AxiomPhys::CircleCollider>(radius);
		AxiomPhys::CircleCollider* ptr = collider.get();
		m_World.RegisterCollider(*ptr);

		auto bodyIt = m_Bodies.find(key);
		if (bodyIt != m_Bodies.end()
			&& m_AttachedColliderKind.find(key) == m_AttachedColliderKind.end()) {
			m_World.AttachCollider(*bodyIt->second, *ptr);
			m_AttachedColliderKind[key] = FastColliderKind::Circle;
		}

		m_Colliders[ColliderKey{ key, FastColliderKind::Circle }] = std::move(collider);
		return ptr;
	}

	void AxiomPhysicsWorld2D::DestroyCollider(EntityHandle entity, FastColliderKind kind) {
		const uint32_t key = static_cast<uint32_t>(entity);
		auto it = m_Colliders.find(ColliderKey{ key, kind });
		if (it == m_Colliders.end()) return;

		AxiomPhys::Collider* ptr = it->second.get();

		// Only detach if THIS kind is the one currently attached to the
		// body. Detaching when a different kind is attached would silently
		// strip the wrong collider from the body's collision state.
		auto bodyIt = m_Bodies.find(key);
		auto attachedIt = m_AttachedColliderKind.find(key);
		const bool wasAttached =
			bodyIt != m_Bodies.end()
			&& attachedIt != m_AttachedColliderKind.end()
			&& attachedIt->second == kind;
		if (wasAttached) {
			m_World.DetachCollider(*bodyIt->second);
			m_AttachedColliderKind.erase(attachedIt);
		}

		m_World.UnregisterCollider(*ptr);
		m_Colliders.erase(it);

		// If a different-kind collider still exists on this entity (the
		// dual-collider case made possible by deserialization), promote
		// it to the active attachment so the body keeps colliding.
		if (wasAttached && bodyIt != m_Bodies.end()) {
			const FastColliderKind otherKind =
				kind == FastColliderKind::Box ? FastColliderKind::Circle : FastColliderKind::Box;
			auto otherIt = m_Colliders.find(ColliderKey{ key, otherKind });
			if (otherIt != m_Colliders.end()) {
				m_World.AttachCollider(*bodyIt->second, *otherIt->second);
				m_AttachedColliderKind[key] = otherKind;
			}
		}
	}

	void AxiomPhysicsWorld2D::DestroyAllCollidersOnEntity(EntityHandle entity) {
		const uint32_t key = static_cast<uint32_t>(entity);
		auto bodyIt = m_Bodies.find(key);
		if (bodyIt != m_Bodies.end()) {
			m_World.DetachCollider(*bodyIt->second);
		}
		m_AttachedColliderKind.erase(key);
		for (auto it = m_Colliders.begin(); it != m_Colliders.end(); ) {
			if (it->first.entity == key) {
				m_World.UnregisterCollider(*it->second);
				it = m_Colliders.erase(it);
			} else {
				++it;
			}
		}
	}

	void AxiomPhysicsWorld2D::RegisterContactCallback(EntityHandle entity, AxiomContactCallback callback) {
		m_ContactCallbacks[static_cast<uint32_t>(entity)] = std::move(callback);
	}

	void AxiomPhysicsWorld2D::UnregisterContactCallback(EntityHandle entity) {
		m_ContactCallbacks.erase(static_cast<uint32_t>(entity));
	}

	void AxiomPhysicsWorld2D::DispatchContacts() {
		const auto& contacts = m_World.GetContacts();
		for (const auto& contact : contacts) {
			EntityHandle entityA = entt::null;
			EntityHandle entityB = entt::null;

			auto itA = m_BodyToEntity.find(contact.bodyA);
			auto itB = m_BodyToEntity.find(contact.bodyB);
			if (itA != m_BodyToEntity.end()) entityA = itA->second;
			if (itB != m_BodyToEntity.end()) entityB = itB->second;

			AxiomContact2D axiomContact{
				entityA, entityB,
				{ contact.normal.x, contact.normal.y },
				contact.penetration
			};

			// Copy the std::function objects out of the map BEFORE invoking. A callback
			// can call UnregisterContactCallback (or destroy its own entity, which routes
			// here too); erasing the map entry mid-call would destroy the std::function
			// while its operator() is still on the stack. CollisionDispatcher::DispatchSafe
			// uses the same pattern.
			AxiomContactCallback cbA;
			AxiomContactCallback cbB;
			if (entityA != entt::null) {
				auto cbIt = m_ContactCallbacks.find(static_cast<uint32_t>(entityA));
				if (cbIt != m_ContactCallbacks.end()) cbA = cbIt->second;
			}
			if (entityB != entt::null) {
				auto cbIt = m_ContactCallbacks.find(static_cast<uint32_t>(entityB));
				if (cbIt != m_ContactCallbacks.end()) cbB = cbIt->second;
			}

			if (cbA) cbA(axiomContact);
			if (cbB) cbB(axiomContact);
		}
	}

}
