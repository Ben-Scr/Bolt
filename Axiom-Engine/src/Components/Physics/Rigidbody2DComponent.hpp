#pragma once
#include "Collections/Vec2.hpp"
#include <box2d/types.h>

namespace Axiom {
	class Scene;
	class Transform2DComponent;
	enum class BodyType;
}

namespace Axiom {
	class Rigidbody2DComponent {
	public:
		Rigidbody2DComponent() = default;

		void SetBodyType(BodyType bodyType);
		BodyType GetBodyType();

		bool IsAwake() const;

		void SetVelocity(const Vec2& velocity);
		Vec2 GetVelocity() const;

		void SetAngularVelocity(float velocity);
		float GetAngularVelocity() const;

		void SetGravityScale(float gravityScale);
		float GetGravityScale() const;

		void SetMass(float mass);
		float GetMass() const;

		void SetLinearDrag(float value);
		void SetAngularDrag(float value);

		void SetRotation(float radians);
		void SetPosition(const Vec2& position);
		Vec2 GetPosition() const;

		void SetTransform(const Transform2DComponent& tr);

		float GetRotation() const;

		void SetEnabled(bool enabled);

		b2BodyId GetBodyHandle() const;
		bool IsValid() const;

		void Destroy();
	private:
		b2BodyId m_BodyId{ b2_nullBodyId };


		friend class BoxCollider2DComponent;
		friend class Collider2D;
		friend class PhysicsSystem2D;
		friend class Physics2D;
		friend class Scene;
	};
}
