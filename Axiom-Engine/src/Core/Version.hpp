#pragma once

#define AIM_VERSION "2026.1.0"

//
// Build Configuration
//
#if defined(AIM_DEBUG)
#define AIM_BUILD_CONFIG_NAME "Debug"
#elif defined(AIM_RELEASE)
#define AIM_BUILD_CONFIG_NAME "Release"
#elif defined(AIM_DIST)
#define AIM_BUILD_CONFIG_NAME "Dist"
#else
#error Undefined configuration?
#endif

//
// Build Platform
//
#if defined(AIM_PLATFORM_WINDOWS)
#define AIM_BUILD_PLATFORM_NAME "Windows x64"
#elif defined(AIM_PLATFORM_LINUX)
#define AIM_BUILD_PLATFORM_NAME "Linux x64"
#else
#error Unsupported Platform!
#endif

#define AIM_VERSION_LONG "Axiom " AIM_VERSION " (" AIM_BUILD_PLATFORM_NAME " " AIM_BUILD_CONFIG_NAME ")"