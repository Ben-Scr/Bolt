#pragma once

#include "Core/Base.hpp"
#include "Core/Exceptions.hpp"
#include "Core/Log.hpp"

#include <source_location>
#include <string>
#include <string_view>
#include <utility>

#include <spdlog/fmt/fmt.h>

#ifdef AIM_PLATFORM_WINDOWS
#define AIM_DEBUG_BREAK __debugbreak()
#else
#include <csignal>
#define AIM_DEBUG_BREAK raise(SIGTRAP)
#endif

namespace Axiom {

	void ReportAssertionFailure(
		const char* kind,
		const char* expression,
		std::string_view message,
		std::source_location location = std::source_location::current());

	inline std::string BuildAssertMessage(const std::string_view message) {
		return std::string(message);
	}

	inline std::string BuildAssertMessage(const AxiomErrorCode code, const std::string_view message) {
		return fmt::format("[{}] {}", ErrorCodeToString(code), message);
	}

	template <typename T>
	inline std::string BuildAssertMessage(T&& message) {
		return std::string(std::forward<T>(message));
	}

} // namespace Axiom

#ifdef AIM_DEBUG
#define AIM_ENABLE_ASSERTS
#endif

#define AIM_ENABLE_VERIFY

#define AIM_ASSERT(cond, ...) \
	do { \
		if (!(cond)) { \
			::Axiom::ReportAssertionFailure("ASSERT", #cond, ::Axiom::BuildAssertMessage(__VA_ARGS__), std::source_location::current()); \
		} \
	} while (0)

#define AIM_CORE_ASSERT(cond, ...) \
	do { \
		if (!(cond)) { \
			::Axiom::ReportAssertionFailure("CORE_ASSERT", #cond, ::Axiom::BuildAssertMessage(__VA_ARGS__), std::source_location::current()); \
		} \
	} while (0)

#ifdef AIM_ENABLE_VERIFY
#define AIM_VERIFY(cond, ...) \
	do { \
		if (!(cond)) { \
			::Axiom::ReportAssertionFailure("VERIFY", #cond, ::Axiom::BuildAssertMessage(__VA_ARGS__), std::source_location::current()); \
		} \
	} while (0)

#define AIM_CORE_VERIFY(cond, ...) \
	do { \
		if (!(cond)) { \
			::Axiom::ReportAssertionFailure("CORE_VERIFY", #cond, ::Axiom::BuildAssertMessage(__VA_ARGS__), std::source_location::current()); \
		} \
	} while (0)
#else
#define AIM_VERIFY(cond, ...) ((void)(cond))
#define AIM_CORE_VERIFY(cond, ...) ((void)(cond))
#endif