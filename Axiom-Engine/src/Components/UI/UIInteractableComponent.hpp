#pragma once

namespace Axiom {

	// Drives one frame of UI input state for an entity that wants to react
	// to mouse events. UIEventSystem updates these flags every frame from
	// the global Input state plus a hit-test against the entity's
	// RectTransform bounds. Game code (scripts, native systems) reads them
	// for the same kind of "if hovered, show tooltip" / "if clicked, do
	// thing" logic that's idiomatic in any UI toolkit.
	//
	// State semantics — exactly mirrors Unity's IPointerXxxHandler shape so
	// the mental model carries over:
	//
	//   IsHovered      — cursor is currently over this rect (this frame).
	//   IsMouseDown    — left button went DOWN this frame while hovering.
	//                    One-frame edge event; consumed before next frame.
	//   IsClicked      — left button went UP this frame while hovering AND
	//                    the original press was on this same entity. This
	//                    is the "real" click event — same semantics as
	//                    button widgets in every UI lib.
	//   IsMouseUp      — left button went UP this frame while hovering,
	//                    regardless of where the press started. Pairs
	//                    with IsMouseDown for drag-style interactions.
	//   IsPressed      — sticky: true between mouse-down and mouse-up if
	//                    the press started on this entity. Lets sliders /
	//                    drags track without re-checking every frame.
	//
	// Set Interactable = false to opt out of input entirely (greyed-out
	// disabled state, modal blocking, etc.) without removing the component.
	struct UIInteractableComponent {
		bool Interactable = true;

		bool IsHovered = false;
		bool IsMouseDown = false;
		bool IsClicked = false;
		bool IsMouseUp = false;
		bool IsPressed = false;
	};

}
