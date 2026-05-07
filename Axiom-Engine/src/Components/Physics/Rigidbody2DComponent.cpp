#include "pch.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Physics/Box2DWorld.hpp"
#include "Physics/PhysicsSystem2D.hpp"
#include "Physics/CollisionDispatcher.hpp"

namespace Axiom {
	// Mutator guard: every b2Body_* call needs a valid body. The body can be torn down
	// out from under a mutator if a sibling component (BoxCollider2DComponent::Destroy)
	// destroys the shared b2Body and zeroes only its own id field, leaving this one
	// stale. Calling b2Body_* with a stale id mutates an unrelated body that Box2D has
	// since recycled into the same slot — a silent UB hazard the audit caught (H15).
	#define AIM_RB_GUARD() do { if (!IsValid()) return; } while (0)

	void Rigidbody2DComponent::SetBodyType(BodyType bodyType) {
		AIM_RB_GUARD();
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

	BodyType Rigidbody2DComponent::GetBodyType() const {
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
		if (!IsValid()) return false;
		return b2Body_IsAwake(m_BodyId);
	}

	void Rigidbody2DComponent::SetVelocity(const Vec2& velocity) {
		AIM_RB_GUARD();
		b2Body_SetLinearVelocity(m_BodyId, b2Vec2(velocity.x, velocity.y));
	}
	Vec2 Rigidbody2DComponent::GetVelocity() const {
		if (!IsValid()) return Vec2{ 0.0f, 0.0f };
		b2Vec2 b2v = b2Body_GetLinearVelocity(m_BodyId);
		return Vec2{ b2v.x, b2v.y };
	}

	void Rigidbody2DComponent::SetAngularVelocity(float velocity) {
		AIM_RB_GUARD();
		b2Body_SetAngularVelocity(m_BodyId, -velocity);
	}
	float Rigidbody2DComponent::GetAngularVelocity() const {
		if (!IsValid()) return 0.0f;
		return -b2Body_GetAngularVelocity(m_BodyId);
	}

	void Rigidbody2DComponent::SetGravityScale(float gravityScale) {
		AIM_RB_GUARD();
		b2Body_SetGravityScale(m_BodyId, gravityScale);
	}
	float Rigidbody2DComponent::GetGravityScale() const {
		if (!IsValid()) return 0.0f;
		return b2Body_GetGravityScale(m_BodyId);
	}

	void Rigidbody2DComponent::SetMass(float mass) {
		AIM_RB_GUARD();
		auto massData = b2Body_GetMassData(m_BodyId);
		const float previousMass = massData.mass;
		const float newMass = mass > 0.0f ? mass : 0.0f;
		if (previousMass > 0.0f) {
			massData.rotationalInertia *= newMass / previousMass;
		}
		else {
			// Promoting from zero mass: don't zero the rotational inertia (which would
			// silently lock the body's rotation against torque/impulse). Box2D will
			// recompute inertia from attached shapes; if no shapes exist this stays 0
			// but at least we don't actively erase a value the user / shape density
			// might have set.
			// (Audit: Rigidbody2DComponent::SetMass discarded inertia on first call.)
		}
		massData.mass = newMass;
		b2Body_SetMassData(m_BodyId, massData);
	}
	float Rigidbody2DComponent::GetMass() const {
		if (!IsValid()) return 0.0f;
		return b2Body_GetMass(m_BodyId);
	}

	void Rigidbody2DComponent::SetLinearDrag(float value) {
		AIM_RB_GUARD();
		b2Body_SetLinearDamping(m_BodyId, value);
	}
	void Rigidbody2DComponent::SetAngularDrag(float value) {
		AIM_RB_GUARD();
		b2Body_SetAngularDamping(m_BodyId, value);
	}

	void Rigidbody2DComponent::SetRotation(float radians) {
		AIM_RB_GUARD();
		// Negate to match GetRotation's convention (see Transform2DComponent::GetB2Rotation).
		b2Body_SetTransform(m_BodyId, b2Body_GetPosition(m_BodyId), b2Rot(std::cos(radians), -std::sin(radians)));
	}
	void Rigidbody2DComponent::SetPosition(const Vec2& position) {
		AIM_RB_GUARD();
		b2Body_SetTransform(m_BodyId, b2Vec2(position.x, position.y), b2Body_GetRotation(m_BodyId));
	}
	Vec2 Rigidbody2DComponent::GetPosition() const { b2Vec2 b2Pos = IsValid() ? b2Body_GetPosition(m_BodyId) : b2Vec2_zero; return { b2Pos.x, b2Pos.y }; }

	void Rigidbody2DComponent::SetTransform(const Transform2DComponent& tr) {
		AIM_RB_GUARD();
		b2Body_SetTransform(m_BodyId, b2Vec2(tr.Position.x, tr.Position.y), tr.GetB2Rotation());
	}

	float Rigidbody2DComponent::GetRotation() const {
		if (!IsValid()) return 0.0f;
		return -b2Rot_GetAngle(b2Body_GetRotation(m_BodyId));
	}

	void Rigidbody2DComponent::SetEnabled(bool enabled) {
		AIM_RB_GUARD();
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
			// Unregister every attached shape from the contact dispatcher before
			// the body (and its shapes) are destroyed. Without this, the
			// dispatcher's m_begin/m_end/m_hit/m_activeContacts retain stale
			// b2ShapeIds; if Box2D recycles a stored id for a future shape,
			// callbacks fire on the wrong entity.
			if (PhysicsSystem2D::IsInitialized()) {
				constexpr int kInlineShapes = 8;
				b2ShapeId stack[kInlineShapes];
				const int count = b2Body_GetShapeCount(m_BodyId);
				auto& dispatcher = PhysicsSystem2D::GetMainPhysicsWorld().GetDispatcher();
				if (count <= kInlineShapes) {
					b2Body_GetShapes(m_BodyId, stack, count);
					for (int i = 0; i < count; ++i) dispatcher.UnregisterShape(stack[i]);
				} else {
					std::vector<b2ShapeId> heap(static_cast<size_t>(count));
					b2Body_GetShapes(m_BodyId, heap.data(), count);
					for (int i = 0; i < count; ++i) dispatcher.UnregisterShape(heap[i]);
				}
			}
			if (PhysicsSystem2D::IsInitialized()) {
				PhysicsSystem2D::GetMainPhysicsWorld().UnregisterBodyBinding(m_BodyId);
			}
			b2DestroyBody(m_BodyId);
		}

		m_BodyId = b2_nullBodyId;
	}
}
