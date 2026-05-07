#include "pch.hpp"
#include "OpenGL.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace Axiom {
	bool OpenGL::s_IsInitialized = false;

	bool OpenGL::Initialize(const GLInitSpecifications& glInitSpecs) {
		if (s_IsInitialized) return false;
		 
		AIM_ASSERT(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress), AxiomErrorCode::Undefined, "Failed to initialize OpenGL");
		SetClearColor(glInitSpecs.ClearColor);

		Enable(GL_BLEND);
		BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		CullFace(glInitSpecs.CullingMode);

		s_IsInitialized = true;
		return true;
	}
	void OpenGL::BlendFunc(GLenum sFactor, GLenum dFactor) {
		glBlendFunc(sFactor, dFactor);
	}

	void OpenGL::Enable(GLenum glEnum){
		glEnable(glEnum);
	}
	void OpenGL::Disable(GLenum glEnum) {
		glDisable(glEnum);
	}

	void OpenGL::CullFace(GLCullingMode cullingMode) {
		// Scoped enum: explicit cast to GLenum is required since the underlying
		// integer is no longer implicitly convertible.
		if (cullingMode == GLCullingMode::None) {
			glDisable(GL_CULL_FACE);
		}
		else {
			glEnable(GL_CULL_FACE);
			glCullFace(static_cast<GLenum>(cullingMode));
		}
	}

	void OpenGL::SetClearColor(const Color& clearColor) {
		glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
	}

	Color OpenGL::GetClearColor()
	{
		GLfloat c[4] = {};
		glGetFloatv(GL_COLOR_CLEAR_VALUE, c);
		return Color{ c[0], c[1], c[2], c[3] };
	}
}