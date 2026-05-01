#pragma once

#include "Collections/Vec2.hpp"
#include "Scene/EntityHandle.hpp"

namespace Axiom {

	struct AxiomContact2D {
		EntityHandle EntityA = entt::null;
		EntityHandle EntityB = entt::null;
		Vec2 Normal{};
		float Penetration = 0.0f;
	};

} // namespace Axiom
