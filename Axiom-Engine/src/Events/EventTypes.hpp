#pragma once

#include "Core/Base.hpp"

namespace Axiom {

	enum class EventType {
		None = 0,

		// Window events
		WindowClose,
		WindowResize,
		WindowFocus,
		WindowLostFocus,
		WindowMoved,
		WindowMinimize,

		// Application events
		AppTick,
		AppUpdate,
		AppRender,

		// Input: Keyboard
		KeyPressed,
		KeyReleased,
		KeyTyped,

		// Input: Mouse
		MouseButtonPressed,
		MouseButtonReleased,
		MouseMoved,
		MouseScrolled,

		// Scene lifecycle
		ScenePreStart,
		ScenePostStart,
		ScenePreStop,
		ScenePostStop,

		// Editor
		EditorExitPlayMode,
		SelectionChanged,

		// Assets
		AssetReloaded,

		// File drop
		FileDrop,
	};

	enum EventCategory {
		EventCategoryNone = 0,
		EventCategoryApplication = BIT(0),
		EventCategoryInput = BIT(1),
		EventCategoryKeyboard = BIT(2),
		EventCategoryMouse = BIT(3),
		EventCategoryMouseButton = BIT(4),
		EventCategoryScene = BIT(5),
		EventCategoryEditor = BIT(6),
	};

} // namespace Axiom
