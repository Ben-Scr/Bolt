#include "pch.hpp"
#include <Components/Physics/Collider2D.hpp>
#include <Components/General/Transform2DComponent.hpp>

#include <Math/Trigonometry.hpp>


namespace Axiom {
	bool Collider2D::IsValid() const { return b2Body_IsValid(m_BodyId) && b2Shape_IsValid(m_ShapeId); }
	void Collider2D::SetFriction(float friction) { b2Shape_SetFriction(m_ShapeId, friction); }
	float Collider2D::GetFriction() const { return b2Shape_GetFriction(m_ShapeId); }
	void Collider2D::SetBounciness(float bounciness) { b2Shape_SetRestitution(m_ShapeId, bounciness); }
	float Collider2D::GetBounciness() const { return b2Shape_GetRestitution(m_ShapeId); }
	void Collider2D::SetLayer(uint64_t layer) {
		b2Filter filter = b2Shape_GetFilter(m_ShapeId);
		filter.categoryBits = layer;
		b2Shape_SetFilter(m_ShapeId, filter);
	}
	uint64_t Collider2D::GetLayer() const { return b2Shape_GetFilter(m_ShapeId).categoryBits; }
	bool Collider2D::IsEnabled() const { return b2Body_IsEnabled(m_BodyId); }
	bool Collider2D::IsSensor() const { return b2Shape_IsSensor(m_ShapeId); }

	Vec2 Collider2D::GetBodyPosition() const {
		b2Vec2 position = b2Body_GetPosition(m_BodyId);
		return { position.x, position.y };
	}
	float Collider2D::GetRotationDegrees() const {
		return Degrees<float>(b2Rot_GetAngle(b2Body_GetRotation(m_BodyId)));
	}
	float Collider2D::GetRotationRadiant() const {
		return  b2Rot_GetAngle(b2Body_GetRotation(m_BodyId));
	}

	void Collider2D::SetRegisterContacts(bool enabled) {
		b2Shape_EnableContactEvents(m_ShapeId, enabled);
	}
	bool Collider2D::CanRegisterContacts() const { return b2Shape_AreContactEventsEnabled(m_ShapeId); }


	void Collider2D::EnableRotation(bool enabled) {
		b2Shape_SetDensity(m_ShapeId, enabled ? 1.f : 0.f, false);
	}


	void Collider2D::DestroyShape(bool updateBodyMass) {
		const bool hasShapeHandle = b2StoreShapeId(m_ShapeId) != b2StoreShapeId(b2_nullShapeId);
		if (hasShapeHandle) {
			PhysicsSystem2D::GetMainPhysicsWorld().GetDispatcher().UnregisterShape(m_ShapeId);
		}

		if (b2Shape_IsValid(m_ShapeId)) {
			b2DestroyShape(m_ShapeId, updateBodyMass);
		}

		m_ShapeId = b2_nullShapeId;
	}

	void Collider2D::Destroy() {
		DestroyShape(true);
		if (b2Body_IsValid(m_BodyId)) {
			if (PhysicsSystem2D::IsInitialized()) {
				PhysicsSystem2D::GetMainPhysicsWorld().UnregisterBodyBinding(m_BodyId);
			}
			b2DestroyBody(m_BodyId);
		}

		m_BodyId = b2_nullBodyId;
	}

	void Collider2D::SetRotation(float radiant) {
		b2Body_SetTransform(m_BodyId, b2Body_GetPosition(m_BodyId), b2Rot(std::cos(radiant), std::sin(radiant)));
	}
	void Collider2D::SetPositionRotation(const Vec2& position, float radiant) {
		b2Body_SetTransform(m_BodyId, b2Vec2(position.x, position.y), b2Rot(std::cos(radiant), std::sin(radiant)));
	}
	void Collider2D::SetPosition(const Vec2& position) {
		b2Body_SetTransform(m_BodyId, b2Vec2(position.x, position.y), b2Body_GetRotation(m_BodyId));
	}
	void Collider2D::SetTransform(const Transform2DComponent& tr) {
		b2Body_SetTransform(m_BodyId, b2Vec2(tr.Position.x, tr.Position.y), tr.GetB2Rotation());
	}
}
