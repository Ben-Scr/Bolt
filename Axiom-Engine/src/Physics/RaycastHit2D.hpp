#pragma once

#include "Collections/Vec2.hpp"
#include "Scene/EntityHandle.hpp"

namespace Axiom {

	struct RaycastHit2D {
		EntityHandle entity;
		Vec2 point;
		Vec2 normal;
		float distance;
	};

} // namespace Axiom
