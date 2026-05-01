#include "pch.hpp"

#include "Core/Assert.hpp"

#include <cstdlib>

namespace Axiom {

	void ReportAssertionFailure(const char* kind, const char* expression, const std::string_view message, const std::source_location location) {
		const std::string formatted = fmt::format(
			"{} failed: ({}) | {} @ {}:{} ({})",
			kind,
			expression,
			message,
			location.file_name(),
			location.line(),
			location.function_name());

		AIM_CORE_ERROR("{}", formatted);

#ifdef AIM_DEBUG
		AIM_DEBUG_BREAK;
#endif
		std::terminate();
	}

}