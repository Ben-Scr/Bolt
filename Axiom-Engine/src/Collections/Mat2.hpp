#pragma once
#include "Math/Trigonometry.hpp"

#include <glm/mat2x2.hpp>
#include <glm/mat3x3.hpp>

namespace  Axiom {
	using Mat2 = glm::mat2x2;
	using Mat3 = glm::mat3x3;

    inline Mat2 Rotation(float radians) noexcept {
        float c = Cos(radians);
        float s = Sin(radians);
        // glm matrices are column-major. This produces:
        // x' = c*x - s*y, y' = s*x + c*y.
        return Mat2(c, s, -s, c);
    }
}
