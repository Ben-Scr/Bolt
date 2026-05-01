#include "pch.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Physics/Box2DWorld.hpp"

namespace Axiom {
	void Rigidbody2DComponent::SetBodyType(BodyType bodyType) {
		switch (bodyType) {
		case BodyType::Static:
			b2Body_SetType(m_BodyId, b2_staticBody);
			break;
		case BodyType::Kinematic:
			b2Body_SetType(m_BodyId, b2_kinematicBody);
			break;
		case BodyType::Dynamic:
			b2Body_SetType(m_BodyId, b2_dynamicBody);
			break;
		}
	}

	BodyType Rigidbody2DComponent::GetBodyType() {
		switch (b2Body_GetType(m_BodyId)) {
		case b2_staticBody:
			return BodyType::Static;
			break;
		case b2_kinematicBody:
			return BodyType::Kinematic;
			break;
		case b2_dynamicBody:
			return BodyType::Dynamic;
		default:
			return BodyType::Static; // ERROR
		}
	}

	bool Rigidbody2DComponent::IsAwake() const {
		return b2Body_IsAwake(m_BodyId);
	}

	void Rigidbody2DComponent::SetVelocity(const Vec2& velocity) { b2Body_SetLinearVelocity(m_BodyId, b2Vec2(velocity.x, velocity.y)); }
	Vec2 Rigidbody2DComponent::GetVelocity() const {
		b2Vec2 b2v = b2Body_GetLinearVelocity(m_BodyId);
		return Vec2{ b2v.x, b2v.y };
	}

	void Rigidbody2DComponent::SetAngularVelocity(float velocity) { b2Body_SetAngularVelocity(m_BodyId, -velocity); }
	float Rigidbody2DComponent::GetAngularVelocity() const { return -b2Body_GetAngularVelocity(m_BodyId); }

	void Rigidbody2DComponent::SetGravityScale(float gravityScale) { b2Body_SetGravityScale(m_BodyId, gravityScale); }
	float Rigidbody2DComponent::GetGravityScale() const { return b2Body_GetGravityScale(m_BodyId); }

	void Rigidbody2DComponent::SetMass(float mass) {
		auto massData = b2Body_GetMassData(m_BodyId);
		massData.mass = mass;
		b2Body_SetMassData(m_BodyId, massData);
	}
	float Rigidbody2DComponent::GetMass() const { return b2Body_GetMass(m_BodyId); }

	void Rigidbody2DComponent::SetLinearDrag(float value) {
		b2Body_SetLinearDamping(m_BodyId, value);
	}
	void Rigidbody2DComponent::SetAngularDrag(float value) {
		b2Body_SetAngularDamping(m_BodyId, value);
	}

	void Rigidbody2DComponent::SetRotation(float radians) { b2Body_SetTransform(m_BodyId, b2Body_GetPosition(m_BodyId), b2Rot(radians)); }
	void Rigidbody2DComponent::SetPosition(const Vec2& position) { b2Body_SetTransform(m_BodyId, b2Vec2(position.x, position.y), b2Body_GetRotation(m_BodyId)); }
	Vec2 Rigidbody2DComponent::GetPosition() const { b2Vec2 b2Pos = IsValid() ? b2Body_GetPosition(m_BodyId) : b2Vec2_zero; return { b2Pos.x, b2Pos.y }; }

	void Rigidbody2DComponent::SetTransform(const Transform2DComponent& tr) { b2Body_SetTransform(m_BodyId, b2Vec2(tr.Position.x, tr.Position.y), tr.GetB2Rotation()); }

	float Rigidbody2DComponent::GetRotation() const { return -b2Rot_GetAngle(b2Body_GetRotation(m_BodyId)); }

	void Rigidbody2DComponent::SetEnabled(bool enabled) {
		if (enabled)
			b2Body_Enable(m_BodyId);
		else
			b2Body_Disable(m_BodyId);
	}

	b2BodyId Rigidbody2DComponent::GetBodyHandle() const {
		if (IsValid())
		{
			return m_BodyId;
		}
		return b2_nullBodyId;
	}

	bool Rigidbody2DComponent::IsValid() const {
		if (!b2Body_IsValid(m_BodyId)) {
			return false;
		}

		return true;
	}

	void Rigidbody2DComponent::Destroy() {
		if (IsValid()) {
			b2DestroyBody(m_BodyId);
		}

		m_BodyId = b2_nullBodyId;
	}
}
