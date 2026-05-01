#pragma once

#include "Shader.hpp"
#include "Collections/Vec2.hpp"
#include "Collections/Color.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <optional>

namespace Axiom {
    class SpriteShaderProgram {
    public:
        void Initialize();
        bool IsValid() const;

        void Bind() const;
        void Unbind() const;
        void Shutdown();

        void SetMVP(const glm::mat4& mvp) const;
        void SetSpritePosition(const Vec2& position) const;
        void SetScale(const Vec2& scale) const;
        void SetRotation(float rotationRadians) const;
        void SetUV(const glm::vec2& offset, const glm::vec2& scale) const;
        void SetPremultipliedAlpha(bool enabled) const;
        void SetAlphaCutoff(float cutoff) const;
        void SetVertexColor(const Color& color) const;

    private:
        void ApplyDefaults() const;

        std::optional<Shader> m_Shader;
        int m_uMVP{ -1 };
        int m_uSpritePos{ -1 };
        int m_uScale{ -1 };
        int m_uRotation{ -1 };
        int m_uUVOffset{ -1 };
        int m_uUVScale{ -1 };
        int m_uPremultipliedAlpha{ -1 };
        int m_uAlphaCutoff{ -1 };
    };
}