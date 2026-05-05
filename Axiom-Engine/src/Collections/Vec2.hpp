#pragma once
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <ostream>

namespace Axiom {
	using Vec2 = glm::vec2;
	using Vec3 = glm::vec3;
	using Vec2Int = glm::ivec2;
	using Vec3Int = glm::ivec3;

	// Lexicographic compare so Vec2 can be a std::map / std::set key. Defined in
	// Axiom's namespace via free function rather than as an overload in the glm::
	// namespace (where it would be UB to add operators on types we don't own).
	inline bool LessLex(const Vec2& a, const Vec2& b) {
		if (a.x < b.x) return true;
		if (a.x > b.x) return false;
		return a.y < b.y;
	}

	inline std::ostream& operator<<(std::ostream& os, const Vec2& v) {
		return os << "(" << v.x << ", " << v.y << ")";
	}
	inline std::ostream& operator<<(std::ostream& os, const Vec2Int& v) {
		return os << "(" << v.x << ", " << v.y << ")";
	}
	inline std::ostream& operator<<(std::ostream& os, const Vec3& v) {
		return os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
	}
	inline std::ostream& operator<<(std::ostream& os, const Vec3Int& v) {
		return os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
	}
}