#include "pch.hpp"
#include "Collections/AABB.hpp"
#include "Components/General/Transform2DComponent.hpp"

namespace Axiom {
	AABB AABB::FromTransform(const Transform2DComponent& transform) {
		// Fast-path the rotation == 0 case (overwhelmingly common for sprites/particles).
		// Skips IsAxisAligned's fmod + 5-element abs loop entirely.
		if (transform.Rotation == 0.0f) {
			const Vec2 halfExtents{ transform.Scale.x * 0.5f, transform.Scale.y * 0.5f };
			return { transform.Position - halfExtents, transform.Position + halfExtents };
		}
		if (IsAxisAligned(transform.Rotation)) {
			return AABB::Create(
				Vec2(transform.Position.x, transform.Position.y),
				Vec2(transform.Scale.x * 0.5f, transform.Scale.y * 0.5f)
			);
		}
		return AABB::Create(
			transform.Position,
			transform.Scale * 0.5f,
			transform.GetRotationDegrees()
		);
	}
}
