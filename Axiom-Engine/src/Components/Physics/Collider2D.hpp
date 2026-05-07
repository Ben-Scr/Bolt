#pragma once
#include "Core/Export.hpp"
#include "Rigidbody2DComponent.hpp"
#include <Core/Assert.hpp>
#include "Physics/PhysicsSystem2D.hpp"
#include "Physics/Box2DWorld.hpp"
#include "Scene/EntityHandle.hpp"
#include "Core/Exceptions.hpp"
#include <box2d/box2d.h>



namespace Axiom {
	struct Collision2D;

	class AXIOM_API Collider2D {
	public:
		Collider2D() = default;
		explicit Collider2D(EntityHandle entity)
			: m_EntityHandle(entity) {}

		bool IsValid() const;
		void SetFriction(float friction);
		float GetFriction() const;
		void SetBounciness(float bounciness);
		float GetBounciness() const;
		void SetLayer(uint64_t layer);
		uint64_t GetLayer() const;
		bool IsEnabled() const;
		bool IsSensor() const;

		Vec2 GetBodyPosition() const;
		float GetRotationDegrees() const;

		void SetRegisterContacts(bool enabled);
		bool CanRegisterContacts() const;

		template<typename F>
		void OnCollisionEnter(F&& callback) {
			AIM_ASSERT(CanRegisterContacts(), AxiomErrorCode::InvalidArgument,
				"Failed to register OnCollisionEnter event: contact events are not enabled for this shape. "
				"Make sure to call collider.SetRegisterContacts(true); before registering event callbacks."
			);

			ContactBeginCallback cb(callback);
			PhysicsSystem2D::GetMainPhysicsWorld().GetDispatcher().RegisterBegin(m_ShapeId, std::move(cb));
		}
		template<typename F>
		void OnCollisionExit(F&& callback) {
			AIM_ASSERT(CanRegisterContacts(), AxiomErrorCode::InvalidArgument,
				"Failed to register OnCollisionExit event: contact events are not enabled for this shape. "
				"Make sure to call collider.SetRegisterContacts(true); before registering event callbacks."
			);

			PhysicsSystem2D::GetMainPhysicsWorld().GetDispatcher().RegisterEnd(m_ShapeId, std::forward<F>(callback));
		}
		template<typename F>
		void OnCollisionHit(F&& callback) {
			AIM_ASSERT(CanRegisterContacts(), AxiomErrorCode::InvalidArgument,
				"Failed to register OnCollisionHit event: contact events are not enabled for this shape. "
				"Make sure to call collider.SetRegisterContacts(true); before registering event callbacks."
			);

			PhysicsSystem2D::GetMainPhysicsWorld().GetDispatcher().RegisterHit(m_ShapeId, std::forward<F>(callback));
		}

		void EnableRotation(bool enabled);


		float GetRotationRadiant() const;

		// Virtual so derived colliders (BoxCollider2DComponent, ...) can Destroy()-shadow
		// without slicing — calling Destroy() through a Collider2D* must reach the
		// derived override (e.g. so derived per-shape members get reset).
		virtual ~Collider2D() = default;
		virtual void Destroy();
		virtual void DestroyShape(bool updateBodyMass = true);

		b2BodyId m_BodyId{ b2_nullBodyId };
		b2ShapeId m_ShapeId{ b2_nullShapeId };

		EntityHandle m_EntityHandle{ entt::null };
	private:
		void SetRotation(float radiant);
		void SetPositionRotation(const Vec2& position, float radiant);
		void SetPosition(const Vec2& position);
		void SetTransform(const Transform2DComponent& tr);

		friend class Physics2D;
	};
}
