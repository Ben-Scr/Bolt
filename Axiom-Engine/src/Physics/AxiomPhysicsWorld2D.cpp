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

		// Detach colliders and unregister everything before clearing ownership
		for (auto& [key, body] : m_Bodies) {
			m_World.DetachCollider(*body);
			m_World.UnregisterBody(*body);
		}
		for (auto& [key, collider] : m_Colliders) {
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

		// Destroy collider first if exists
		DestroyCollider(entity);

		auto it = m_Bodies.find(key);
		if (it == m_Bodies.end()) return;

		AxiomPhys::Body* ptr = it->second.get();
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

		DestroyCollider(entity); // Replace existing

		auto collider = std::make_unique<AxiomPhys::BoxCollider>(AxiomPhys::Vec2(halfExtents.x, halfExtents.y));
		AxiomPhys::BoxCollider* ptr = collider.get();
		m_World.RegisterCollider(*ptr);

		// Attach to body if one exists
		auto bodyIt = m_Bodies.find(key);
		if (bodyIt != m_Bodies.end()) {
			m_World.AttachCollider(*bodyIt->second, *ptr);
		}

		m_Colliders[key] = std::move(collider);
		return ptr;
	}

	AxiomPhys::CircleCollider* AxiomPhysicsWorld2D::CreateCircleCollider(EntityHandle entity, float radius) {
		uint32_t key = static_cast<uint32_t>(entity);

		DestroyCollider(entity);

		auto collider = std::make_unique<AxiomPhys::CircleCollider>(radius);
		AxiomPhys::CircleCollider* ptr = collider.get();
		m_World.RegisterCollider(*ptr);

		auto bodyIt = m_Bodies.find(key);
		if (bodyIt != m_Bodies.end()) {
			m_World.AttachCollider(*bodyIt->second, *ptr);
		}

		m_Colliders[key] = std::move(collider);
		return ptr;
	}

	void AxiomPhysicsWorld2D::DestroyCollider(EntityHandle entity) {
		uint32_t key = static_cast<uint32_t>(entity);
		auto it = m_Colliders.find(key);
		if (it == m_Colliders.end()) return;

		AxiomPhys::Collider* ptr = it->second.get();

		// Detach from body if attached
		auto bodyIt = m_Bodies.find(key);
		if (bodyIt != m_Bodies.end()) {
			m_World.DetachCollider(*bodyIt->second);
		}

		m_World.UnregisterCollider(*ptr);
		m_Colliders.erase(it);
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

			// Dispatch to both entities
			if (entityA != entt::null) {
				auto cbIt = m_ContactCallbacks.find(static_cast<uint32_t>(entityA));
				if (cbIt != m_ContactCallbacks.end()) cbIt->second(axiomContact);
			}
			if (entityB != entt::null) {
				auto cbIt = m_ContactCallbacks.find(static_cast<uint32_t>(entityB));
				if (cbIt != m_ContactCallbacks.end()) cbIt->second(axiomContact);
			}
		}
	}

}
