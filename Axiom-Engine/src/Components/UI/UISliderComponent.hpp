#pragma once

#include "Scene/EntityHandle.hpp"

#include <entt/entt.hpp>

namespace Axiom {

	// Horizontal slider widget state. UIEventSystem owns the dragging
	// state machine: while the user holds the mouse down inside the
	// track's RectTransform, Value is updated every frame from the
	// cursor's X position relative to the track. Game code reads
	// Value (and the convenience flag ValueChangedThisFrame) instead of
	// re-implementing the drag math.
	//
	// HandleEntity is the optional child entity used as the visual thumb;
	// the slider system repositions it along the track every frame. Leave
	// it as entt::null to draw the slider track-only (useful when the
	// thumb is implemented some other way, e.g. a fill bar).
	struct UISliderComponent {
		float Value = 0.5f;       // current normalized value in [MinValue, MaxValue]
		float MinValue = 0.0f;
		float MaxValue = 1.0f;
		bool WholeNumbers = false;

		EntityHandle HandleEntity = entt::null;

		// Set to true by UIEventSystem on the frame Value moved. Cleared
		// at the start of the next event-system tick so callers don't
		// have to remember a "previous value" themselves.
		bool ValueChangedThisFrame = false;
	};

}
