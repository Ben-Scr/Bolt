#pragma once

#define BT_VERSION "2026.1.0"

//
// Build Configuration
//
#if defined(BT_DEBUG)
#define BT_BUILD_CONFIG_NAME "Debug"
#elif defined(BT_RELEASE)
#define BT_BUILD_CONFIG_NAME "Release"
#elif defined(BT_DIST)
#define BT_BUILD_CONFIG_NAME "Dist"
#else
#error Undefined configuration?
#endif

//
// Build Platform
//
#if defined(BT_PLATFORM_WINDOWS)
#define BT_BUILD_PLATFORM_NAME "Windows x64"
#elif defined(BT_PLATFORM_LINUX)
#define BT_BUILD_PLATFORM_NAME "Linux x64"
#else
#error Unsupported Platform!
#endif

#define BT_VERSION_LONG "Bolt " BT_VERSION " (" BT_BUILD_PLATFORM_NAME " " BT_BUILD_CONFIG_NAME ")"