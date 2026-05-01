#pragma once
#include "Collections/Color.hpp"
#include "Core/Export.hpp"
#include "Graphics/GLInitSpecifications.hpp"

#include <glad/glad.h>

namespace Axiom {
	class AXIOM_API OpenGL {
	public:
		OpenGL() = delete;

		static bool Initialize(const GLInitSpecifications& glInitSpecs);
		static bool IsInitialized() { return s_IsInitialized; }
		static void BlendFunc(GLenum sFactor, GLenum dFactor);
		static void Enable(GLenum glEnum);
		static void Disable(GLenum glEnum);
		static void CullFace(GLCullingMode cullingMode);
		static void SetClearColor(const Color& clearColor);
		static Color GetClearColor();
	private:
		static bool s_IsInitialized;
	};
}
