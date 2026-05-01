#pragma once

#include <cstdint>
#include <ostream>

namespace Axiom {

	enum class MouseButton : uint8_t {
		Left = 0, Right = 1, Middle = 2, Back = 3, Revert = 4, Button6 = 5, Button7 = 6, Button8 = 7
	};

	inline std::ostream& operator<<(std::ostream& os, MouseButton button) {
		os << static_cast<int32_t>(button);
		return os;
	}

}
