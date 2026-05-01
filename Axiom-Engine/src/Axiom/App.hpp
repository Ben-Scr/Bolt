#pragma once

// Full runtime application umbrella. Requires AXIOM_WITH_APPLICATION=1, which
// is enabled by the full module profile or an equivalent custom profile.
// Include EntryPoint.hpp separately in the single translation unit that owns
// the process entry point.

#include "Axiom/Core.hpp"
#include "Core/Application.hpp"
