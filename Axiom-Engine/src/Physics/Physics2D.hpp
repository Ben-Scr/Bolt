#pragma once
#include "Collections/Vec2.hpp"
#include "Physics/OverlapMode.hpp"
#include "Physics/RaycastHit2D.hpp"
#include "Scene/EntityHandle.hpp"

#include <optional>
#include <vector>

namespace Axiom {
	class Rigidbody2DComponent;
	class Collider2D;
	class Scene;
}

namespace Axiom {
	struct PhysicsBodyRef2D {
		EntityHandle entity = entt::null;
		Scene* scene = nullptr;
	};

	class Physics2D {
	public:
		[[deprecated("Use OverlapCircleRef for scene-aware results.")]]
		static std::optional<EntityHandle> OverlapCircle(const Vec2& center, float radius, OverlapMode mode);
		[[deprecated("Use OverlapBoxRef for scene-aware results.")]]
		static std::optional<EntityHandle> OverlapBox(const Vec2& center, const Vec2& halfExtents, float degrees, OverlapMode mode);
		[[deprecated("Use OverlapPolygonRef for scene-aware results.")]]
		static std::optional<EntityHandle> OverlapPolygon(const Vec2& center, const std::vector<Vec2>& points, OverlapMode mode);
		[[deprecated("Use ContainsPointRef for scene-aware results.")]]
		static std::optional<EntityHandle> ContainsPoint(const Vec2& point, OverlapMode mode);
		static std::optional<PhysicsBodyRef2D> OverlapCircleRef(const Vec2& center, float radius, OverlapMode mode);
		static std::optional<PhysicsBodyRef2D> OverlapBoxRef(const Vec2& center, const Vec2& halfExtents, float degrees, OverlapMode mode);
		static std::optional<PhysicsBodyRef2D> OverlapPolygonRef(const Vec2& center, const std::vector<Vec2>& points, OverlapMode mode);
		static std::optional<PhysicsBodyRef2D> ContainsPointRef(const Vec2& point, OverlapMode mode);
		static std::optional<RaycastHit2D> Raycast(const Vec2& origin, const Vec2& direction, float maxDistance);
		[[deprecated("Use OverlapCircleAllRefs for scene-aware results.")]]
		static std::vector<EntityHandle> OverlapCircleAll(const Vec2& center, float radius);
		[[deprecated("Use OverlapBoxAllRefs for scene-aware results.")]]
		static std::vector<EntityHandle> OverlapBoxAll(const Vec2& center, const Vec2& halfExtents, float degrees);
		[[deprecated("Use OverlapPolygonAllRefs for scene-aware results.")]]
		static std::vector<EntityHandle> OverlapPolygonAll(const Vec2& center, const std::vector<Vec2>& points);
		[[deprecated("Use ContainsPointAllRefs for scene-aware results.")]]
		static std::vector<EntityHandle> ContainsPointAll(const Vec2& point);
		static std::vector<PhysicsBodyRef2D> OverlapCircleAllRefs(const Vec2& center, float radius);
		static std::vector<PhysicsBodyRef2D> OverlapBoxAllRefs(const Vec2& center, const Vec2& halfExtents, float degrees);
		static std::vector<PhysicsBodyRef2D> OverlapPolygonAllRefs(const Vec2& center, const std::vector<Vec2>& points);
		static std::vector<PhysicsBodyRef2D> ContainsPointAllRefs(const Vec2& point);
	};
}
