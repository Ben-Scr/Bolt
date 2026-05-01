#pragma once

#include "Core/Export.hpp"
#include "Core/WindowSpecification.hpp"

#ifndef AXIOM_DEFAULT_ENABLE_IMGUI
#define AXIOM_DEFAULT_ENABLE_IMGUI 1
#endif

#ifndef AXIOM_DEFAULT_ENABLE_GUI_RENDERER
#define AXIOM_DEFAULT_ENABLE_GUI_RENDERER 1
#endif

#ifndef AXIOM_DEFAULT_ENABLE_GIZMO_RENDERER
#define AXIOM_DEFAULT_ENABLE_GIZMO_RENDERER 1
#endif

#ifndef AXIOM_DEFAULT_ENABLE_PHYSICS_2D
#define AXIOM_DEFAULT_ENABLE_PHYSICS_2D 1
#endif

#ifndef AXIOM_DEFAULT_ENABLE_AUDIO
#define AXIOM_DEFAULT_ENABLE_AUDIO 1
#endif

#ifndef AXIOM_DEFAULT_SET_WINDOW_ICON
#define AXIOM_DEFAULT_SET_WINDOW_ICON 1
#endif

#ifndef AXIOM_DEFAULT_ENABLE_VSYNC
#define AXIOM_DEFAULT_ENABLE_VSYNC 1
#endif

namespace Axiom {

	struct ApplicationConfig {
		WindowSpecification WindowSpecification{ 800, 800, "Axiom Runtime", true, true, false };
		// These are runtime feature requests; build-time module availability is described by the
		// dependency sets in Dependencies.lua and the feature flags in Core/Export.hpp.
		bool EnableImGui = AXIOM_DEFAULT_ENABLE_IMGUI != 0;
		bool EnableGuiRenderer = AXIOM_DEFAULT_ENABLE_GUI_RENDERER != 0;
		bool EnableGizmoRenderer = AXIOM_DEFAULT_ENABLE_GIZMO_RENDERER != 0;
		bool EnablePhysics2D = AXIOM_DEFAULT_ENABLE_PHYSICS_2D != 0;
		bool EnableAudio = AXIOM_DEFAULT_ENABLE_AUDIO != 0;
		bool SetWindowIcon = AXIOM_DEFAULT_SET_WINDOW_ICON != 0;
		bool Vsync = AXIOM_DEFAULT_ENABLE_VSYNC != 0;
		bool UseTargetFrameRateForMainLoop = true;
	};

} // namespace Axiom
