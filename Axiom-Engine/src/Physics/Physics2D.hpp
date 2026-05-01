#pragma once
#include "Collections/Vec2.hpp"
#include "Physics/OverlapMode.hpp"
#include "Physics/RaycastHit2D.hpp"

#include <optional>
#include <vector>

namespace Axiom {
	class Rigidbody2DComponent;
	class Collider2D;
}

namespace Axiom {
	class Physics2D {
	public:
		static std::optional<EntityHandle> OverlapCircle(const Vec2& center, float radius, OverlapMode mode);
		static std::optional<EntityHandle> OverlapBox(const Vec2& center, const Vec2& halfExtents, float degrees, OverlapMode mode);
		static std::optional<EntityHandle> OverlapPolygon(const Vec2& center, const std::vector<Vec2>& points, OverlapMode mode);
		static std::optional<EntityHandle> ContainsPoint(const Vec2& point, OverlapMode mode);
		static std::optional<RaycastHit2D> Raycast(const Vec2& origin, const Vec2& direction, float maxDistance);
		static std::vector<EntityHandle> OverlapCircleAll(const Vec2& center, float radius);
		static std::vector<EntityHandle> OverlapBoxAll(const Vec2& center, const Vec2& halfExtents, float degrees);
		static std::vector<EntityHandle> OverlapPolygonAll(const Vec2& center, const std::vector<Vec2>& points);
		static std::vector<EntityHandle> ContainsPointAll(const Vec2& point);
	};
}
