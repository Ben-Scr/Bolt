#pragma once
#include "Collider2D.hpp"
#include "Rigidbody2DComponent.hpp"
#include "Physics/PhysicsSystem2D.hpp"

#include <box2d/box2d.h>

namespace Axiom {
	class AXIOM_API BoxCollider2DComponent : public Collider2D {
		friend class PhysicsSystem2D;
	public:
		BoxCollider2DComponent() = default;
		explicit BoxCollider2DComponent(EntityHandle entity)
			: Collider2D(entity) {}

		void SetScale(const Vec2& scale, const Scene& scene);
		void SetEnabled(bool enabled);
		void SetSensor(bool sensor, Scene& scene);
		Vec2 GetScale() const;
		Vec2 GetLocalScale(const Scene& scene) const;

		// Note: has to be called when the transforms scale has been changed.
		void UpdateScale(const Scene& scene);

		void SetCenter(const Vec2& center, const Scene& scene);
		Vec2 GetCenter() const;
	private:
	};
}
