#pragma once

// Public feature/export contract for Axiom core consumers:
// - AXIOM_API / AXIOM_*_API describe symbol visibility
// - AXIOM_WITH_* describes which optional modules the current translation unit opted into
// - AXIOM_CORE_ONLY is true when no optional module flags are enabled

#if defined(AIM_PLATFORM_WINDOWS)
#if defined(AIM_BUILD_DLL)
#define AXIOM_API __declspec(dllexport)
#elif defined(AIM_IMPORT_DLL)
#define AXIOM_API __declspec(dllimport)
#else
#define AXIOM_API
#endif
#else
#define AXIOM_API
#endif

#if defined(AXIOM_ALL_MODULES)
#undef AXIOM_WITH_RENDER
#undef AXIOM_WITH_AUDIO
#undef AXIOM_WITH_PHYSICS
#undef AXIOM_WITH_SCRIPTING
#undef AXIOM_WITH_EDITOR
#undef AXIOM_WITH_APPLICATION
#define AXIOM_WITH_RENDER 1
#define AXIOM_WITH_AUDIO 1
#define AXIOM_WITH_PHYSICS 1
#define AXIOM_WITH_SCRIPTING 1
#define AXIOM_WITH_EDITOR 1
#define AXIOM_WITH_APPLICATION 1
#endif

#ifndef AXIOM_WITH_RENDER
#define AXIOM_WITH_RENDER 0
#endif

#ifndef AXIOM_WITH_AUDIO
#define AXIOM_WITH_AUDIO 0
#endif

#ifndef AXIOM_WITH_PHYSICS
#define AXIOM_WITH_PHYSICS 0
#endif

#ifndef AXIOM_WITH_SCRIPTING
#define AXIOM_WITH_SCRIPTING 0
#endif

#ifndef AXIOM_WITH_EDITOR
#define AXIOM_WITH_EDITOR 0
#endif

#ifndef AXIOM_WITH_APPLICATION
#define AXIOM_WITH_APPLICATION 0
#endif

#if !AXIOM_WITH_RENDER && !AXIOM_WITH_AUDIO && !AXIOM_WITH_PHYSICS && !AXIOM_WITH_SCRIPTING && !AXIOM_WITH_EDITOR
#define AXIOM_CORE_ONLY 1
#else
#define AXIOM_CORE_ONLY 0
#endif

#define AXIOM_CORE_API AXIOM_API
#define AXIOM_RENDER_API AXIOM_API
#define AXIOM_AUDIO_API AXIOM_API
#define AXIOM_PHYSICS_API AXIOM_API
#define AXIOM_SCRIPTING_API AXIOM_API
#define AXIOM_EDITOR_API AXIOM_API
