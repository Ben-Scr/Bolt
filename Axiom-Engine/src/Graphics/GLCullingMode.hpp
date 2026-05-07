#pragma once

#include <cstdint>

namespace Axiom {

	// Only the three values glCullFace accepts as a face selector. The previous
	// enum exposed glDrawBuffer/glReadBuffer constants (FrontLeft/BackLeft/Left/
	// Right/etc.); passing one of those into OpenGL::CullFace produced a silent
	// GL_INVALID_ENUM and disabled culling. Keeping the enum tight prevents that
	// foot-gun at the type-system level.
	enum class GLCullingMode : uint32_t {
		None             = 0,
		Front            = 0x0404,    // GL_FRONT
		Back             = 0x0405,    // GL_BACK
		FrontAndBack     = 0x0408,    // GL_FRONT_AND_BACK
	};

} // namespace Axiom
