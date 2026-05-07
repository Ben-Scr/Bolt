#pragma once

#include "Scene/EntityHandle.hpp"

#include <entt/entt.hpp>

namespace Axiom {

	// Boolean toggle (checkbox) widget state. CheckmarkEntity is the
	// child entity whose ImageComponent should be drawn only when
	// IsOn is true — UIEventSystem flips its enabled-state every
	// frame to match. Leave entt::null if the on/off look is handled
	// some other way (e.g. by tinting the parent image directly).
	//
	// ValueChangedThisFrame is set on the frame the toggle flips,
	// cleared at the start of the next tick.
	struct ToggleComponent {
		bool IsOn = false;
		EntityHandle CheckmarkEntity = entt::null;

		bool ValueChangedThisFrame = false;
	};

}
