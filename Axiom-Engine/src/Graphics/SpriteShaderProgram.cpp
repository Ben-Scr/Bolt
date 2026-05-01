#include "pch.hpp"
#include "SpriteShaderProgram.hpp"
#include "Serialization/Path.hpp"

#include <glm/gtc/type_ptr.hpp>

namespace Axiom {
	void SpriteShaderProgram::Initialize() {
		std::string shaderDir = Path::ResolveAxiomAssets("Shader");
		if (shaderDir.empty()) {
			AIM_CORE_ERROR("AxiomAssets/Shader not found");
			shaderDir = Path::Combine(Path::ExecutableDir(), "AxiomAssets", "Shader");
		}

		m_Shader.emplace(Path::Combine(shaderDir, "2D/sprite.vert.glsl"), Path::Combine(shaderDir, "2D/sprite.frag.glsl"));

		AIM_ASSERT(IsValid(), AxiomErrorCode::Undefined, "Failed to create sprite shader program");

		Bind();

		GLuint handle = m_Shader.value().GetHandle();
		m_uMVP = glGetUniformLocation(handle, "uMVP");
		m_uSpritePos = glGetUniformLocation(handle, "uSpritePos");
		m_uScale = glGetUniformLocation(handle, "uScale");
		m_uRotation = glGetUniformLocation(handle, "uRotation");
		m_uUVOffset = glGetUniformLocation(handle, "uUVOffset");
		m_uUVScale = glGetUniformLocation(handle, "uUVScale");
		m_uPremultipliedAlpha = glGetUniformLocation(handle, "uPremultipliedAlpha");
		m_uAlphaCutoff = glGetUniformLocation(handle, "uAlphaCutoff");

		int locTex = glGetUniformLocation(handle, "uTexture");
		if (locTex >= 0) {
			glUniform1i(locTex, 0);
		}

		ApplyDefaults();
		Unbind();
	}

	bool SpriteShaderProgram::IsValid() const {
		return m_Shader.has_value() && m_Shader->IsValid();
	}

	void SpriteShaderProgram::Bind() const {
		if (IsValid()) {
			m_Shader->Submit();
		}
	}

	void SpriteShaderProgram::Unbind() const {
		glUseProgram(0);
	}

	void SpriteShaderProgram::Shutdown() {
		m_Shader.reset();
		m_uMVP = m_uSpritePos = m_uScale = m_uRotation = -1;
		m_uUVOffset = m_uUVScale = m_uPremultipliedAlpha = m_uAlphaCutoff = -1;
	}

	void SpriteShaderProgram::SetMVP(const glm::mat4& mvp) const {
		if (m_uMVP >= 0) {
			glUniformMatrix4fv(m_uMVP, 1, GL_FALSE, glm::value_ptr(mvp));
		}
	}

	void SpriteShaderProgram::SetSpritePosition(const Vec2& position) const {
		if (m_uSpritePos >= 0) {
			glUniform2f(m_uSpritePos, position.x, position.y);
		}
	}

	void SpriteShaderProgram::SetScale(const Vec2& scale) const {
		if (m_uScale >= 0) {
			glUniform2f(m_uScale, scale.x, scale.y);
		}
	}

	void SpriteShaderProgram::SetRotation(float rotationRadians) const {
		if (m_uRotation >= 0) {
			glUniform1f(m_uRotation, rotationRadians);
		}
	}

	void SpriteShaderProgram::SetUV(const glm::vec2& offset, const glm::vec2& scale) const {
		if (m_uUVOffset >= 0) {
			glUniform2f(m_uUVOffset, offset.x, offset.y);
		}
		if (m_uUVScale >= 0) {
			glUniform2f(m_uUVScale, scale.x, scale.y);
		}
	}

	void SpriteShaderProgram::SetPremultipliedAlpha(bool enabled) const {
		if (m_uPremultipliedAlpha >= 0) {
			glUniform1i(m_uPremultipliedAlpha, enabled ? 1 : 0);
		}
	}

	void SpriteShaderProgram::SetAlphaCutoff(float cutoff) const {
		if (m_uAlphaCutoff >= 0) {
			glUniform1f(m_uAlphaCutoff, cutoff);
		}
	}

	void SpriteShaderProgram::SetVertexColor(const Color& color) const {
		glVertexAttrib4f(2, color.r, color.g, color.b, color.a);
	}

	void SpriteShaderProgram::ApplyDefaults() const {
		SetUV(glm::vec2(0.0f), glm::vec2(1.0f));
		SetPremultipliedAlpha(false);
		SetAlphaCutoff(0.0f);
	}
}