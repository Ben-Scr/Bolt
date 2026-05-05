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

#if defined(AIM_DEBUG) || defined(AIM_RELEASE)
		// Debug: hit the debugger if attached. Release: same — Release builds are
		// developer builds and we want fast-fail. Dist (shipped) builds log the
		// failure and try to keep running so a single bad assert doesn't kill
		// the user's session.
		AIM_DEBUG_BREAK;
		std::terminate();
#elif defined(AIM_DIST)
		// Shipped build: report and continue. The error log stream above is the
		// only feedback path for end users — don't terminate the process.
#else
		std::terminate();
#endif
	}

}