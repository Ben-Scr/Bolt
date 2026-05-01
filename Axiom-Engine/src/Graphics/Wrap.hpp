#pragma once

#include "Core/Export.hpp"

#include <cstdint>
#include <magic_enum/magic_enum.hpp>

namespace Axiom {

	enum class AXIOM_API Wrap : uint32_t { Repeat = 0x2901, Clamp = 0x812F, Mirror = 0x8370, Border = 0x812D };

} // namespace Axiom

template <>
struct magic_enum::customize::enum_range<Axiom::Wrap> {
	static constexpr int min = 0x2901;
	static constexpr int max = 0x8370;
};
