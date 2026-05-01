#pragma once
#include "Collections/Vec2.hpp"

namespace Axiom {
	struct RectTransformComponent {
		Vec2 Position;
		Vec2 Pivot{ 0.0f, 0.0f };
		float Width{ 100 }, Height{ 100 };
		float Rotation{ 0.f };
		Vec2 Scale{ 1.f, 1.f };

		const Vec2 GetSize() const {
			return Vec2(Width, Height) * Scale;
		}

		const Vec2 GetBottomLeft() const {
			Vec2 size = GetSize();
			return {
				Position.x - size.x * Pivot.x,
				Position.y - size.y * Pivot.y
			};
		}
	};
}