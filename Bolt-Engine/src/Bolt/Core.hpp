#pragma once

// Standalone core umbrella. This header intentionally excludes the full
// application/runtime stack, renderer, audio, physics, scripting, and editor.

// Collections
#include "Collections/Color.hpp"
#include "Collections/Mat2.hpp"
#include "Collections/Vec2.hpp"
#include "Collections/Vec4.hpp"
#include "Collections/Viewport.hpp"

// Core
#include "Core/ApplicationConfig.hpp"
#include "Core/Assert.hpp"
#include "Core/Base.hpp"
#include "Core/Exceptions.hpp"
#include "Core/Export.hpp"
#include "Core/Log.hpp"
#include "Core/Time.hpp"
#include "Core/Version.hpp"
