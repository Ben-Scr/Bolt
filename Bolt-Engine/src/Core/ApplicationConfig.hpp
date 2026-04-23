#pragma once

#include "Core/Export.hpp"
#include "Core/WindowSpecification.hpp"

#ifndef BOLT_DEFAULT_ENABLE_IMGUI
#define BOLT_DEFAULT_ENABLE_IMGUI 1
#endif

#ifndef BOLT_DEFAULT_ENABLE_GUI_RENDERER
#define BOLT_DEFAULT_ENABLE_GUI_RENDERER 1
#endif

#ifndef BOLT_DEFAULT_ENABLE_GIZMO_RENDERER
#define BOLT_DEFAULT_ENABLE_GIZMO_RENDERER 1
#endif

#ifndef BOLT_DEFAULT_ENABLE_PHYSICS_2D
#define BOLT_DEFAULT_ENABLE_PHYSICS_2D 1
#endif

#ifndef BOLT_DEFAULT_ENABLE_AUDIO
#define BOLT_DEFAULT_ENABLE_AUDIO 1
#endif

#ifndef BOLT_DEFAULT_SET_WINDOW_ICON
#define BOLT_DEFAULT_SET_WINDOW_ICON 1
#endif

#ifndef BOLT_DEFAULT_ENABLE_VSYNC
#define BOLT_DEFAULT_ENABLE_VSYNC 1
#endif

namespace Bolt {

	struct ApplicationConfig {
		WindowSpecification WindowSpecification{ 800, 800, "Bolt Runtime", true, true, false };
		// These are runtime feature requests; build-time module availability is described by the
		// dependency sets in Dependencies.lua and the feature flags in Core/Export.hpp.
		bool EnableImGui = BOLT_DEFAULT_ENABLE_IMGUI != 0;
		bool EnableGuiRenderer = BOLT_DEFAULT_ENABLE_GUI_RENDERER != 0;
		bool EnableGizmoRenderer = BOLT_DEFAULT_ENABLE_GIZMO_RENDERER != 0;
		bool EnablePhysics2D = BOLT_DEFAULT_ENABLE_PHYSICS_2D != 0;
		bool EnableAudio = BOLT_DEFAULT_ENABLE_AUDIO != 0;
		bool SetWindowIcon = BOLT_DEFAULT_SET_WINDOW_ICON != 0;
		bool Vsync = BOLT_DEFAULT_ENABLE_VSYNC != 0;
	};

} // namespace Bolt
