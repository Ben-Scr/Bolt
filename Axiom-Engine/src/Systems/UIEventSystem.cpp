#include "pch.hpp"
#include "Systems/UIEventSystem.hpp"

#include "Collections/Viewport.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/Tags.hpp"
#include "Components/UI/ButtonComponent.hpp"
#include "Components/UI/DropdownComponent.hpp"
#include "Components/UI/InputFieldComponent.hpp"
#include "Components/UI/InteractableComponent.hpp"
#include "Components/UI/SliderComponent.hpp"
#include "Components/UI/ToggleComponent.hpp"
#include "Core/Application.hpp"
#include "Core/Input.hpp"
#include "Core/Log.hpp"
#include "Scene/Scene.hpp"

#include <algorithm>

namespace Axiom {

	namespace {

		// Convert raw (top-left origin, +Y down) GLFW pixel coords into
		// the centred-origin, +Y-up screen space the GuiRenderer uses for
		// UI layout. Matches the ortho transform in GuiRenderer::RenderScene.
		Vec2 ScreenPixelToUiSpace(const Vec2& mouseRaw, int viewportWidth, int viewportHeight) {
			return Vec2{
				mouseRaw.x - static_cast<float>(viewportWidth) * 0.5f,
				static_cast<float>(viewportHeight) * 0.5f - mouseRaw.y
			};
		}

		// Resolve a button's visual color for the current state.
		Color ResolveButtonTint(const ButtonComponent& btn,
			const InteractableComponent& interact)
		{
			if (!interact.Interactable) return btn.DisabledColor;
			if (interact.IsPressed)     return btn.PressedColor;
			if (interact.IsHovered)     return btn.HoveredColor;
			return btn.NormalColor;
		}

	}

	void UIEventSystem::Update(Scene& scene) {
		Application* app = Application::GetInstance();
		if (!app) return;
		Input& input = app->GetInput();

		// We need the viewport to convert raw mouse pixels into UI space.
		// Use the scene's main camera's viewport — same one GuiRenderer
		// renders against — so hit-tests align with what the user sees.
		const Camera2DComponent* camera = scene.GetMainCamera();
		Viewport* viewport = camera ? const_cast<Camera2DComponent*>(camera)->GetViewport() : nullptr;
		if (!viewport) return;

		const Vec2 mouseUi = ScreenPixelToUiSpace(
			input.GetMousePosition(), viewport->GetWidth(), viewport->GetHeight());
		const bool mouseDownThisFrame = input.GetMouseDown(MouseButton::Left);
		const bool mouseUpThisFrame   = input.GetMouseUp(MouseButton::Left);
		const bool mouseHeld          = input.GetMouse(MouseButton::Left);

		auto& registry = scene.GetRegistry();

		// ── 1. Hit-test: find the front-most interactable rect under the
		//    cursor. Today there's no z-ordering for UI separate from
		//    sprite sorting, so we just take the last one in iteration
		//    order — matches GuiRenderer's draw order.
		EntityHandle hovered = entt::null;
		auto hitView = registry.view<RectTransform2DComponent, InteractableComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, rect, interact] : hitView.each()) {
			if (!interact.Interactable) continue;
			if (rect.ContainsPoint(mouseUi)) {
				hovered = entity;
			}
		}

		// ── 2. Per-entity state machine. Reset edge events first so
		//    entities that *aren't* under the cursor still see their
		//    previous-frame edges cleared.
		for (auto&& [entity, rect, interact] : hitView.each()) {
			interact.IsHovered     = (entity == hovered);
			interact.IsMouseDown   = false;
			interact.IsMouseUp     = false;
			interact.IsClicked     = false;

			if (!interact.Interactable) {
				interact.IsPressed = false;
				continue;
			}

			// Press: only the entity under the cursor when the button
			// went down receives a press. We remember which entity it
			// was on the system itself so a drag that leaves the rect
			// still tracks (slider use-case) and a release elsewhere
			// doesn't fire a click on a different rect.
			if (interact.IsHovered && mouseDownThisFrame) {
				interact.IsMouseDown = true;
				interact.IsPressed = true;
				m_PressedEntity = entity;
			}

			// IsPressed stays sticky between down and up — but only
			// while the mouse button is actually held. A release
			// anywhere clears it on the next iteration of this loop.
			if (!mouseHeld) {
				interact.IsPressed = false;
			}

			if (mouseUpThisFrame) {
				if (interact.IsHovered) {
					interact.IsMouseUp = true;
					// Click = down + up on same entity.
					if (m_PressedEntity == entity) {
						interact.IsClicked = true;
					}
				}
			}
		}

		// Reset the system-wide press tracker when the button finally
		// goes up so the next click cycle starts clean.
		if (mouseUpThisFrame) {
			m_PressedEntity = entt::null;
		}

		// ── 3. Visual presets: tint Image colors per state for buttons.
		auto buttonView = registry.view<InteractableComponent, ButtonComponent, ImageComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, interact, btn, image] : buttonView.each()) {
			image.Color = ResolveButtonTint(btn, interact);
		}

		// ── 4. Sliders: while pressed, map cursor X across the track to
		//    the slider's [Min, Max] range. The track is the entity's
		//    own RectTransform2D; the optional handle is just visualised
		//    by stretching its anchor. We update the handle entity's
		//    AnchoredPosition.x every frame so it tracks the value.
		auto sliderView = registry.view<InteractableComponent, SliderComponent, RectTransform2DComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, interact, slider, rect] : sliderView.each()) {
			const float prevValue = slider.Value;
			slider.ValueChangedThisFrame = false;

			if (interact.Interactable && interact.IsPressed) {
				const Vec2 bl = rect.GetBottomLeft();
				const Vec2 size = rect.GetSize();
				if (size.x > 0.0f) {
					float t = (mouseUi.x - bl.x) / size.x;
					t = std::clamp(t, 0.0f, 1.0f);
					float newValue = slider.MinValue + t * (slider.MaxValue - slider.MinValue);
					if (slider.WholeNumbers) {
						newValue = std::round(newValue);
					}
					slider.Value = newValue;
				}
			}

			if (slider.Value != prevValue) {
				slider.ValueChangedThisFrame = true;
			}

			// Update handle entity's position.
			if (slider.HandleEntity != entt::null
				&& registry.valid(slider.HandleEntity)
				&& registry.all_of<RectTransform2DComponent>(slider.HandleEntity))
			{
				auto& handleRect = registry.get<RectTransform2DComponent>(slider.HandleEntity);
				const float range = slider.MaxValue - slider.MinValue;
				const float t = (range != 0.0f) ? (slider.Value - slider.MinValue) / range : 0.0f;
				const float trackWidth = rect.GetSize().x;
				// Place the handle relative to the slider's left edge.
				// AnchoredPosition is parent-relative when hierarchy is
				// wired; for unparented sliders this is a world offset.
				handleRect.AnchoredPosition.x = -trackWidth * 0.5f + trackWidth * t;
			}
		}

		// ── 5. Toggles: flip on click, sync checkmark visibility.
		// Defer DisabledTag mutations until iteration finishes — emplace<>/remove<>
		// during a view that excludes the same component reorganises storage and
		// invalidates iterators (the toggle's CheckmarkEntity may carry the same
		// component types we're iterating over). Collect (entity, desiredEnabled)
		// pairs and apply post-loop.
		auto toggleView = registry.view<InteractableComponent, ToggleComponent>(entt::exclude<DisabledTag>);
		std::vector<std::pair<EntityHandle, bool>> deferredCheckmarkEnable;
		for (auto&& [entity, interact, toggle] : toggleView.each()) {
			toggle.ValueChangedThisFrame = false;
			if (interact.IsClicked) {
				toggle.IsOn = !toggle.IsOn;
				toggle.ValueChangedThisFrame = true;
			}

			if (toggle.CheckmarkEntity != entt::null && registry.valid(toggle.CheckmarkEntity)) {
				deferredCheckmarkEnable.emplace_back(toggle.CheckmarkEntity, toggle.IsOn);
			}
		}
		for (const auto& [checkmark, desiredEnabled] : deferredCheckmarkEnable) {
			if (!registry.valid(checkmark)) continue;
			const bool currentlyDisabled = registry.all_of<DisabledTag>(checkmark);
			if (desiredEnabled && currentlyDisabled) {
				registry.remove<DisabledTag>(checkmark);
			}
			else if (!desiredEnabled && !currentlyDisabled) {
				registry.emplace<DisabledTag>(checkmark);
			}
		}

		// ── 6. Input fields: focus management. Click inside focuses;
		//    click outside (anywhere mouseDown happened this frame
		//    that wasn't an input field) defocuses.
		auto inputView = registry.view<InteractableComponent, InputFieldComponent>(entt::exclude<DisabledTag>);
		bool clickedAnyInputField = false;
		for (auto&& [entity, interact, field] : inputView.each()) {
			field.SubmittedThisFrame = false;
			if (interact.IsClicked) {
				field.IsFocused = true;
				clickedAnyInputField = true;
			}
		}
		if (mouseDownThisFrame && !clickedAnyInputField) {
			for (auto&& [entity, interact, field] : inputView.each()) {
				field.IsFocused = false;
			}
		}

		// ── 7. Dropdowns: click toggles open/close.
		auto dropdownView = registry.view<InteractableComponent, DropdownComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, interact, dropdown] : dropdownView.each()) {
			dropdown.SelectionChangedThisFrame = false;
			if (interact.IsClicked) {
				dropdown.IsOpen = !dropdown.IsOpen;
			}
		}
	}

}
