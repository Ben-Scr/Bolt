#pragma once
#include "Core/Export.hpp"
#include "Debugging/LogLevel.hpp"
#include "Utils/Event.hpp"

#include <source_location>
#include <string>

namespace Axiom {
	class AXIOM_API Logger {
	public:
		static void Message(const std::string& message, const std::source_location& loc = std::source_location::current());
		static void Message(const std::string& topic, const std::string& message, const std::source_location& loc = std::source_location::current());

		static void Warning(const std::string& message, const std::source_location& loc = std::source_location::current());
		static void Warning(const std::string& topic, const std::string& message, const std::source_location& loc = std::source_location::current());

		static void Error(const std::string& message, const std::source_location& loc = std::source_location::current());
		static void Error(const std::string& topic, const std::string& message, const std::source_location& loc = std::source_location::current());

	
		static Event<const std::string&, LogLevel> OnLog;
	private:
		static std::string LogLevelToString(LogLevel logLevel) {
			switch (logLevel) {
			case LogLevel::None: return "None";
			case LogLevel::Info: return "Info";
			case LogLevel::Warning: return "Warning";
			case LogLevel::Error: return "Error";
			}
			return "Unknown";
		}
		static void Log(const std::string& topic, const std::string& message, LogLevel logLevel, const std::source_location& loc = std::source_location::current());
		static void Log(const std::string& message, LogLevel logLevel, const std::source_location& loc = std::source_location::current());
	};

	inline void Print(const std::string& message, const std::source_location& loc = std::source_location::current()) {
		Logger::Message(message, loc);
	}
	inline void PrintWarning(const std::string& message, const std::source_location& loc = std::source_location::current()) {
		Logger::Warning(message, loc);
	}
	inline void PrintError(const std::string& message, const std::source_location& loc = std::source_location::current()) {
		Logger::Error(message, loc);
	}
}
