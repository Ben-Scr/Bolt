#pragma once

// Public feature/export contract for Bolt core consumers:
// - BOLT_API / BOLT_*_API describe symbol visibility
// - BOLT_WITH_* describes which optional modules the current translation unit opted into
// - BOLT_CORE_ONLY is true when no optional module flags are enabled

#if defined(BT_PLATFORM_WINDOWS)
#if defined(BT_BUILD_DLL)
#define BOLT_API __declspec(dllexport)
#elif defined(BT_IMPORT_DLL)
#define BOLT_API __declspec(dllimport)
#else
#define BOLT_API
#endif
#else
#define BOLT_API
#endif

#if defined(BOLT_ALL_MODULES)
#undef BOLT_WITH_RENDER
#undef BOLT_WITH_AUDIO
#undef BOLT_WITH_PHYSICS
#undef BOLT_WITH_SCRIPTING
#undef BOLT_WITH_EDITOR
#define BOLT_WITH_RENDER 1
#define BOLT_WITH_AUDIO 1
#define BOLT_WITH_PHYSICS 1
#define BOLT_WITH_SCRIPTING 1
#define BOLT_WITH_EDITOR 1
#endif

#ifndef BOLT_WITH_RENDER
#define BOLT_WITH_RENDER 0
#endif

#ifndef BOLT_WITH_AUDIO
#define BOLT_WITH_AUDIO 0
#endif

#ifndef BOLT_WITH_PHYSICS
#define BOLT_WITH_PHYSICS 0
#endif

#ifndef BOLT_WITH_SCRIPTING
#define BOLT_WITH_SCRIPTING 0
#endif

#ifndef BOLT_WITH_EDITOR
#define BOLT_WITH_EDITOR 0
#endif

#if !BOLT_WITH_RENDER && !BOLT_WITH_AUDIO && !BOLT_WITH_PHYSICS && !BOLT_WITH_SCRIPTING && !BOLT_WITH_EDITOR
#define BOLT_CORE_ONLY 1
#else
#define BOLT_CORE_ONLY 0
#endif

#define BOLT_CORE_API BOLT_API
#define BOLT_RENDER_API BOLT_API
#define BOLT_AUDIO_API BOLT_API
#define BOLT_PHYSICS_API BOLT_API
#define BOLT_SCRIPTING_API BOLT_API
#define BOLT_EDITOR_API BOLT_API
