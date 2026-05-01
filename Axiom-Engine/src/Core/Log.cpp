#include "pch.hpp"

#include "Core/Log.hpp"

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Axiom {

	Event<const Log::Entry&> Log::OnLog;

	void Log::Initialize() {
		std::scoped_lock lock(s_StateMutex);
		if (s_Initialized) {
			return;
		}

		std::vector<spdlog::sink_ptr> sinks;
		sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
		sinks.emplace_back(std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg& msg) {
			if (!OnLog.HasListeners()) {
				return;
			}

			Entry entry{};
			entry.Message = fmt::to_string(msg.payload);
			switch (msg.level) {
			case spdlog::level::trace: entry.Level = Level::Trace; break;
			case spdlog::level::info: entry.Level = Level::Info; break;
			case spdlog::level::warn: entry.Level = Level::Warn; break;
			case spdlog::level::err: entry.Level = Level::Error; break;
			case spdlog::level::critical: entry.Level = Level::Critical; break;
			default: entry.Level = Level::Info; break;
			}
			// Determine source type from logger name
			auto name = fmt::to_string(msg.logger_name);
			if (name == "APP") entry.Source = Type::Client;
			else if (name == "EDITOR") entry.Source = Type::EditorConsole;
			else entry.Source = Type::Core;
			OnLog.Invoke(entry);
			}));

		auto setupLogger = [&](const std::string& name) {
			auto logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
			logger->set_level(spdlog::level::trace);
			logger->flush_on(spdlog::level::warn);
			return logger;
			};

		s_CoreLogger = setupLogger("AXIOM");
		s_ClientLogger = setupLogger("APP");
		s_EditorConsoleLogger = setupLogger("EDITOR");

		s_CoreLogger->set_pattern("[%T] [%^%n%$] %v");
		s_ClientLogger->set_pattern("[%T] [%^%n%$] %v");
		s_EditorConsoleLogger->set_pattern("[%T] [%^%n%$] %v");

		spdlog::set_default_logger(s_CoreLogger);
		s_Initialized = true;
	}

	void Log::Shutdown() {
		std::scoped_lock lock(s_StateMutex);
		if (!s_Initialized) {
			return;
		}

		OnLog.Clear();
		s_CoreLogger.reset();
		s_ClientLogger.reset();
		s_EditorConsoleLogger.reset();
		spdlog::shutdown();
		s_Initialized = false;
	}

	bool Log::IsInitialized() {
		std::scoped_lock lock(s_StateMutex);
		return s_Initialized;
	}

	std::shared_ptr<spdlog::logger> Log::GetCoreLogger() {
		EnsureInitialized();
		std::scoped_lock lock(s_StateMutex);
		return s_CoreLogger;
	}

	std::shared_ptr<spdlog::logger> Log::GetClientLogger() {
		EnsureInitialized();
		std::scoped_lock lock(s_StateMutex);
		return s_ClientLogger;
	}

	std::shared_ptr<spdlog::logger> Log::GetEditorConsoleLogger() {
		EnsureInitialized();
		std::scoped_lock lock(s_StateMutex);
		return s_EditorConsoleLogger;
	}

	void Log::PrintMessage(const Type type, const Level level, const std::string_view message) {
		if (!EnsureInitialized()) {
			return;
		}
		auto logger = SelectLogger(type);
		if (!logger || !logger->should_log(ToSpdlogLevel(level))) {
			return;
		}
		Emit(logger, level, message);
	}

	void Log::PrintMessageTag(const Type type, const Level level, const std::string_view tag, const std::string_view message) {
		if (!EnsureInitialized()) {
			return;
		}
		auto logger = SelectLogger(type);
		if (!logger || !logger->should_log(ToSpdlogLevel(level))) {
			return;
		}
		Emit(logger, level, fmt::format("[{}] {}", tag, message));
	}

	const char* Log::LevelToString(const Level level) {
		switch (level) {
		case Level::Trace: return "Trace";
		case Level::Info: return "Info";
		case Level::Warn: return "Warn";
		case Level::Error: return "Error";
		case Level::Critical: return "Critical";
		default: return "Unknown";
		}
	}

	bool Log::EnsureInitialized() {
		{
			std::scoped_lock lock(s_StateMutex);
			if (s_Initialized) {
				return true;
			}
		}

		Initialize();

		std::scoped_lock lock(s_StateMutex);
		return s_Initialized;
	}

	std::shared_ptr<spdlog::logger> Log::SelectLogger(const Type type) {
		std::scoped_lock lock(s_StateMutex);
		switch (type) {
		case Type::Core: return s_CoreLogger;
		case Type::Client: return s_ClientLogger;
		case Type::EditorConsole: return s_EditorConsoleLogger;
		default: return s_CoreLogger;
		}
	}

	spdlog::level::level_enum Log::ToSpdlogLevel(const Level level) {
		switch (level) {
		case Level::Trace: return spdlog::level::trace;
		case Level::Info: return spdlog::level::info;
		case Level::Warn: return spdlog::level::warn;
		case Level::Error: return spdlog::level::err;
		case Level::Critical: return spdlog::level::critical;
		default: return spdlog::level::info;
		}
	}

	bool Log::ShouldLog(const Type type, const Level level) {
		auto logger = SelectLogger(type);
		return logger && logger->should_log(ToSpdlogLevel(level));
	}

	void Log::Emit(std::shared_ptr<spdlog::logger>& logger, const Level level, const std::string_view message) {
		if (!logger) {
			return;
		}

		switch (ToSpdlogLevel(level)) {
		case spdlog::level::trace: logger->trace(message); break;
		case spdlog::level::info: logger->info(message); break;
		case spdlog::level::warn: logger->warn(message); break;
		case spdlog::level::err: logger->error(message); break;
		case spdlog::level::critical: logger->critical(message); break;
		default: logger->info(message); break;
		}
	}

}
