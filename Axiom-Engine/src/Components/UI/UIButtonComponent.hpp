#pragma once

#include "Collections/Color.hpp"

namespace Axiom {

	// Marker + visual-state preset for a clickable UI button. Pairs with
	// UIInteractableComponent (which provides the input flags) and an
	// ImageComponent on the same entity (which provides the background
	// rect that gets tinted). UIEventSystem applies the appropriate tint
	// per frame: NormalColor when idle, HoveredColor when the cursor is
	// inside, PressedColor while the mouse button is held down on this
	// entity, DisabledColor when UIInteractable.Interactable is false.
	//
	// The button itself is just a state holder — game code reacts to
	// UIInteractable::IsClicked to "do the thing" the button represents.
	struct UIButtonComponent {
		Color NormalColor   { 1.00f, 1.00f, 1.00f, 1.0f };
		Color HoveredColor  { 0.85f, 0.85f, 0.85f, 1.0f };
		Color PressedColor  { 0.65f, 0.65f, 0.65f, 1.0f };
		Color DisabledColor { 0.50f, 0.50f, 0.50f, 0.5f };
	};

}
