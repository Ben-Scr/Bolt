#pragma once
#include "Core/AxiomErrorCode.hpp"

#include <exception>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

#include "Core/Log.hpp"

namespace Axiom {
	const char* ErrorCodeToString(AxiomErrorCode code);

	class AxiomException : public std::runtime_error {
	public:
		AxiomException(AxiomErrorCode code, std::string message, std::source_location location = std::source_location::current());

		AxiomErrorCode Code() const noexcept { return m_Code; }
		const std::string& Message() const noexcept { return m_Message; }
		const std::source_location& Location() const noexcept { return m_Location; }

	private:
		static std::string BuildWhat(AxiomErrorCode code, std::string_view message, const std::source_location& location);

		AxiomErrorCode m_Code;
		std::string m_Message;
		std::source_location m_Location;
	};

	using AxiomError = AxiomException;

	[[noreturn]] void ThrowError(AxiomErrorCode code, std::string_view message, std::source_location location = std::source_location::current());
	[[noreturn]] void RethrowWithContext(AxiomErrorCode code, std::string message, std::source_location location = std::source_location::current());

} // namespace Axiom

#define AIM_THROW(code, msg) ::Axiom::ThrowError((code), (msg), std::source_location::current())

#define AIM_LOG_ERROR(code, msg) \
	do { \
		AIM_CORE_ERROR("[{}] {}", ::Axiom::ErrorCodeToString(code), (msg)); \
	} while (0)

#define AXIOM_LOG_ERROR_IF(cond, code, msg) \
	do { \
		if (cond) { \
			AIM_LOG_ERROR((code), (msg)); \
		} \
	} while (0)

#define AXIOM_TRY_CATCH_LOG(stmt) \
	do { \
		try { \
			stmt; \
		} catch (const std::exception& ex) { \
			AIM_CORE_ERROR("Exception: {}", ex.what()); \
		} catch (...) { \
			AIM_CORE_ERROR("Unknown exception"); \
		} \
	} while (0)
