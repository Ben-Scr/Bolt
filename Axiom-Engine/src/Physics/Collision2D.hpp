#pragma once
#include "Collections/Vec2.hpp"
#include "Scene/EntityHandle.hpp"

namespace Axiom {
	struct Collision2D {
		EntityHandle entityA;
		EntityHandle entityB;
		Vec2 contactPoint{ 0.0f, 0.0f };
	};
}
