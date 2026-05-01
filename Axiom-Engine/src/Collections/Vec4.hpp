#pragma once
#include <cmath>
#include <glm/vec4.hpp>

namespace Axiom {
	using Vec4 = glm::vec4;

	inline float Dot(const Vec4& a, const Vec4& b) {
		return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
	}
	inline float Length(const Vec4& v) {
		return std::sqrt(Dot(v, v));
	}
	inline Vec4 Normalize(const Vec4& v) {
		float len = Length(v);
		return (len > 0.0f) ? (v * (1.0f / len)) : Vec4(0.0f);
	}
}