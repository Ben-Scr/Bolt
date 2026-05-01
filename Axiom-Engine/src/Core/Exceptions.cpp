#include "pch.hpp"

#include "Core/Exceptions.hpp"

#include <utility>
#include <spdlog/fmt/fmt.h>

namespace Axiom {

	const char* ErrorCodeToString(const AxiomErrorCode code) {
		switch (code) {
		case AxiomErrorCode::InvalidArgument: return "InvalidArgument";
		case AxiomErrorCode::NotInitialized: return "NotInitialized";
		case AxiomErrorCode::AlreadyInitialized: return "AlreadyInitialized";
		case AxiomErrorCode::FileNotFound: return "FileNotFound";
		case AxiomErrorCode::InvalidHandle: return "InvalidHandle";
		case AxiomErrorCode::OutOfRange: return "OutOfRange";
		case AxiomErrorCode::OutOfBounds: return "OutOfBounds";
		case AxiomErrorCode::Overflow: return "Overflow";
		case AxiomErrorCode::NullReference: return "NullReference";
		case AxiomErrorCode::LoadFailed: return "LoadFailed";
		case AxiomErrorCode::InvalidValue: return "InvalidValue";
		case AxiomErrorCode::Undefined: return "Undefined";
		default: return "Undefined";
		}
	}

	AxiomException::AxiomException(const AxiomErrorCode code, std::string message, const std::source_location location)
		: std::runtime_error(BuildWhat(code, message, location)),
		m_Code(code),
		m_Message(std::move(message)),
		m_Location(location) {
	}

	std::string AxiomException::BuildWhat(const AxiomErrorCode code, const std::string_view message, const std::source_location& location) {
		return fmt::format("AxiomException[{}] {} @ {}:{} ({})",
			ErrorCodeToString(code),
			message,
			location.file_name(),
			location.line(),
			location.function_name());
	}

	[[noreturn]] void ThrowError(const AxiomErrorCode code, const std::string_view message, const std::source_location location) {
		throw AxiomException(code, std::string(message), location);
	}

	[[noreturn]] void RethrowWithContext(const AxiomErrorCode code, std::string message, const std::source_location location) {
		try {
			throw;
		}
		catch (const std::exception& ex) {
			message += std::string(" | caused by: ") + ex.what();
			throw AxiomException(code, std::move(message), location);
		}
		catch (...) {
			message += " | caused by: <non-std exception>";
			throw AxiomException(code, std::move(message), location);
		}
	}

} // namespace Axiom