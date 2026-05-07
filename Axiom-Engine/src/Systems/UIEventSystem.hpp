#pragma once
#include "Scene/EntityHandle.hpp"
#include "Scene/ISystem.hpp"

#include <entt/entt.hpp>

namespace Axiom {

	// Per-frame UI input state-machine. Walks every entity that has a
	// RectTransform2DComponent + InteractableComponent and:
	//   - hit-tests the mouse against the rect (in GuiRenderer's
	//     centred screen space — same convention RectTransform2D's
	//     AnchoredPosition uses today),
	//   - tracks press / release transitions so the IsClicked one-frame
	//     edge fires only when press AND release happened on the same
	//     entity (the standard "real click" semantics),
	//   - applies the per-state tint preset for ButtonComponent,
	//   - updates SliderComponent::Value while dragging,
	//   - toggles ToggleComponent::IsOn / shows/hides its checkmark,
	//   - manages InputFieldComponent::IsFocused on click,
	//   - opens / closes DropdownComponent::IsOpen on click.
	//
	// Game code reads InteractableComponent's IsClicked / IsHovered /
	// IsPressed flags every frame to drive its own logic — UIEventSystem
	// only owns the input-state mechanics.
	class UIEventSystem : public ISystem {
	public:
		void Update(Scene& scene) override;

	private:
		// Track which entity we pressed on so a release elsewhere doesn't
		// register as a click on a different rect, and so a slider drag
		// keeps tracking the cursor even when it leaves the track.
		EntityHandle m_PressedEntity = entt::null;
	};

}
