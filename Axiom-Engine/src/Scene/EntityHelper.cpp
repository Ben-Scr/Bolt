#include "pch.hpp"
#include "EntityHelper.hpp"

#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/General/NameComponent.hpp"
#include "Components/Tags.hpp"
#include "Components/UI/ButtonComponent.hpp"
#include "Components/UI/DropdownComponent.hpp"
#include "Components/UI/InputFieldComponent.hpp"
#include "Components/UI/InteractableComponent.hpp"
#include "Components/UI/SliderComponent.hpp"
#include "Components/UI/ToggleComponent.hpp"

namespace Axiom {
	void EntityHelper::SetEnabled(Entity entity, bool enabled) {
		if (!enabled && !entity.HasComponent<DisabledTag>())
			entity.AddComponent<DisabledTag>();
		else if (enabled && entity.HasComponent<DisabledTag>())
			entity.RemoveComponent<DisabledTag>();
	}
	bool EntityHelper::IsEnabled(Entity entity) {
		return !entity.HasComponent<DisabledTag>();
	}

	std::size_t EntityHelper::EntitiesCount() {
		std::size_t count = 0;

		SceneManager::Get().ForeachLoadedScene([&](const Scene& scene) {
			count += scene.GetRegistry().view<EntityHandle>().size();
			});

		return count;
	}

	Entity EntityHelper::CreateCamera2DEntity(Scene& scene) {
		Entity entity = CreateWith<Transform2DComponent, Camera2DComponent>(scene);
		entity.AddComponent<NameComponent>(NameComponent("Camera 2D"));
		return entity;
	}

	Entity EntityHelper::CreateCamera2DEntity() {
		Entity entity = CreateWith<Transform2DComponent, Camera2DComponent>();
		entity.AddComponent<NameComponent>(NameComponent("Camera 2D"));
		return entity;
	}

	Entity EntityHelper::CreateSpriteEntity(Scene& scene) {
		return CreateWith<Transform2DComponent, SpriteRendererComponent>(scene);
	}

	Entity EntityHelper::CreateSpriteEntity() {
		return CreateWith<Transform2DComponent, SpriteRendererComponent >();
	}

	Entity EntityHelper::CreateImageEntity(Scene& scene) {
		return CreateWith<RectTransform2DComponent, ImageComponent>(scene);
	}

	Entity EntityHelper::CreateImageEntity() {
		return CreateWith<RectTransform2DComponent, ImageComponent>();
	}

	// ── UI presets ──────────────────────────────────────────────────────
	// Each preset configures a "parent" entity (the widget root) plus
	// optional child entities for visuals that need their own rect /
	// renderer. Sizes are picked to look reasonable at the default
	// 100-unit-per-major-element scale GuiRenderer uses (= 1 unit per
	// pixel). The user can resize after spawning.

	Entity EntityHelper::CreateUIPanel(Scene& scene) {
		Entity entity = CreateWith<RectTransform2DComponent, ImageComponent>(scene);
		entity.AddComponent<NameComponent>(NameComponent("Panel"));
		auto& img = entity.GetComponent<ImageComponent>();
		img.Color = Color{ 0.2f, 0.2f, 0.2f, 0.8f };
		auto& rect = entity.GetComponent<RectTransform2DComponent>();
		rect.SizeDelta = Vec2{ 300.0f, 200.0f };
		return entity;
	}

	Entity EntityHelper::CreateUIButton(Scene& scene) {
		Entity entity = CreateWith<RectTransform2DComponent, ImageComponent,
			InteractableComponent, ButtonComponent>(scene);
		entity.AddComponent<NameComponent>(NameComponent("Button"));
		auto& rect = entity.GetComponent<RectTransform2DComponent>();
		rect.SizeDelta = Vec2{ 160.0f, 40.0f };
		// Image starts white; UIEventSystem retints from ButtonComponent
		// every frame, so the explicit color here is just the value at
		// rest before the first system tick.
		entity.GetComponent<ImageComponent>().Color = Color{ 1.0f, 1.0f, 1.0f, 1.0f };

		// Label child. RectTransform2D anchors centred so the text sits
		// inside the parent regardless of how the button is resized.
		Entity label = CreateWith<RectTransform2DComponent, TextRendererComponent>(scene);
		label.AddComponent<NameComponent>(NameComponent("Label"));
		auto& labelRect = label.GetComponent<RectTransform2DComponent>();
		labelRect.SizeDelta = Vec2{ 160.0f, 40.0f };
		auto& labelText = label.GetComponent<TextRendererComponent>();
		labelText.Text = "Button";
		labelText.Color = Color{ 0.0f, 0.0f, 0.0f, 1.0f };
		labelText.HAlign = TextAlignment::Center;
		labelText.FontSize = 20.0f;
		label.SetParent(entity);

		return entity;
	}

	Entity EntityHelper::CreateUISlider(Scene& scene) {
		Entity entity = CreateWith<RectTransform2DComponent, ImageComponent,
			InteractableComponent, SliderComponent>(scene);
		entity.AddComponent<NameComponent>(NameComponent("Slider"));
		auto& rect = entity.GetComponent<RectTransform2DComponent>();
		rect.SizeDelta = Vec2{ 200.0f, 16.0f };
		entity.GetComponent<ImageComponent>().Color = Color{ 0.25f, 0.25f, 0.25f, 1.0f };

		// Handle child — the bit the user visually drags. Width is
		// fixed; UIEventSystem repositions it along the track every
		// frame from SliderComponent::Value.
		Entity handle = CreateWith<RectTransform2DComponent, ImageComponent>(scene);
		handle.AddComponent<NameComponent>(NameComponent("Handle"));
		auto& handleRect = handle.GetComponent<RectTransform2DComponent>();
		handleRect.SizeDelta = Vec2{ 16.0f, 24.0f };
		handle.GetComponent<ImageComponent>().Color = Color{ 0.85f, 0.85f, 0.85f, 1.0f };
		handle.SetParent(entity);

		entity.GetComponent<SliderComponent>().HandleEntity = handle.GetHandle();
		return entity;
	}

	Entity EntityHelper::CreateUIInputField(Scene& scene) {
		Entity entity = CreateWith<RectTransform2DComponent, ImageComponent,
			InteractableComponent, InputFieldComponent>(scene);
		entity.AddComponent<NameComponent>(NameComponent("Input Field"));
		auto& rect = entity.GetComponent<RectTransform2DComponent>();
		rect.SizeDelta = Vec2{ 220.0f, 32.0f };
		entity.GetComponent<ImageComponent>().Color = Color{ 0.95f, 0.95f, 0.95f, 1.0f };

		Entity textChild = CreateWith<RectTransform2DComponent, TextRendererComponent>(scene);
		textChild.AddComponent<NameComponent>(NameComponent("Text"));
		auto& textRect = textChild.GetComponent<RectTransform2DComponent>();
		textRect.SizeDelta = Vec2{ 220.0f, 32.0f };
		auto& tc = textChild.GetComponent<TextRendererComponent>();
		tc.Text = "Enter text...";
		tc.Color = Color{ 0.4f, 0.4f, 0.4f, 1.0f };
		tc.FontSize = 16.0f;
		tc.HAlign = TextAlignment::Left;
		textChild.SetParent(entity);

		return entity;
	}

	Entity EntityHelper::CreateUIDropdown(Scene& scene) {
		Entity entity = CreateWith<RectTransform2DComponent, ImageComponent,
			InteractableComponent, DropdownComponent>(scene);
		entity.AddComponent<NameComponent>(NameComponent("Dropdown"));
		auto& rect = entity.GetComponent<RectTransform2DComponent>();
		rect.SizeDelta = Vec2{ 200.0f, 32.0f };
		entity.GetComponent<ImageComponent>().Color = Color{ 0.95f, 0.95f, 0.95f, 1.0f };

		auto& dd = entity.GetComponent<DropdownComponent>();
		dd.Options = { "Option 1", "Option 2", "Option 3" };

		Entity labelChild = CreateWith<RectTransform2DComponent, TextRendererComponent>(scene);
		labelChild.AddComponent<NameComponent>(NameComponent("Label"));
		auto& labelRect = labelChild.GetComponent<RectTransform2DComponent>();
		labelRect.SizeDelta = Vec2{ 200.0f, 32.0f };
		auto& tc = labelChild.GetComponent<TextRendererComponent>();
		tc.Text = dd.Options.empty() ? "" : dd.Options[0];
		tc.Color = Color{ 0.0f, 0.0f, 0.0f, 1.0f };
		tc.FontSize = 16.0f;
		tc.HAlign = TextAlignment::Left;
		labelChild.SetParent(entity);

		return entity;
	}

	Entity EntityHelper::CreateUIToggle(Scene& scene) {
		Entity entity = CreateWith<RectTransform2DComponent, ImageComponent,
			InteractableComponent, ToggleComponent>(scene);
		entity.AddComponent<NameComponent>(NameComponent("Toggle"));
		auto& rect = entity.GetComponent<RectTransform2DComponent>();
		rect.SizeDelta = Vec2{ 24.0f, 24.0f };
		entity.GetComponent<ImageComponent>().Color = Color{ 0.95f, 0.95f, 0.95f, 1.0f };

		// Checkmark child — UIEventSystem flips it enabled/disabled based
		// on ToggleComponent::IsOn. Defaults disabled because IsOn = false.
		Entity check = CreateWith<RectTransform2DComponent, ImageComponent>(scene);
		check.AddComponent<NameComponent>(NameComponent("Checkmark"));
		auto& checkRect = check.GetComponent<RectTransform2DComponent>();
		checkRect.SizeDelta = Vec2{ 16.0f, 16.0f };
		check.GetComponent<ImageComponent>().Color = Color{ 0.15f, 0.6f, 0.15f, 1.0f };
		check.AddComponent<DisabledTag>();
		check.SetParent(entity);

		entity.GetComponent<ToggleComponent>().CheckmarkEntity = check.GetHandle();
		return entity;
	}
}
