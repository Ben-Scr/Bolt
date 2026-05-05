#pragma once
#include <cmath>
#include <ostream>
#include "Core/Export.hpp"

namespace Axiom {
	struct AXIOM_API Quaternion {
		float x, y, z, w;
		Quaternion(float x = 0.0f, float y = 0.0f, float z = 0.0f, float w = 1.0f)
			: x(x), y(y), z(z), w(w) {
		}
		void Normalize() {
			const float length = std::sqrt(x * x + y * y + z * z + w * w);
			if (length > 0.0f) {
				x /= length;
				y /= length;
				z /= length;
				w /= length;
			}
			else {
				// Zero-length quaternion has no defined orientation; falling back to
				// identity is the only sensible choice (silently no-oping leaves the
				// caller with a (0,0,0,0) "rotation" that misbehaves everywhere downstream).
				x = 0.0f; y = 0.0f; z = 0.0f; w = 1.0f;
			}
		}
	};

	inline std::ostream& operator<<(std::ostream& os, const Quaternion& quat) {
		return os << "(" << quat.x << ", " << quat.y << ", " << quat.z << ", " << quat.w << ")";
	}
}