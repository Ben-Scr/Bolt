#pragma once

#include "Collections/Color.hpp"
#include "Graphics/GLCullingMode.hpp"

namespace Axiom {

	struct GLInitSpecifications {
		Color ClearColor;
		GLCullingMode CullingMode;

		GLInitSpecifications(const Color& clearColor, GLCullingMode cullMode)
			: ClearColor{ clearColor }, CullingMode{ cullMode } {
		}
	};

} // namespace Axiom
