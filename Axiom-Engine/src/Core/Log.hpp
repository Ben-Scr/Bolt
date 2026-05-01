#pragma once

#include "Core/Base.hpp"
#include "Utils/Event.hpp"

#include <spdlog/fmt/fmt.h>
#include <spdlog/logger.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace Axiom {

	class Log {
	public:
		enum class Type : uint8_t {
			Core = 0,
			Client,
			EditorConsole
		};

		enum class Level : uint8_t {
			Trace = 0,
			Info,
			Warn,
			Error,
			Critical
		};

		struct Entry {
			std::string Message;
			Level Level = Level::Info;
			Type Source = Type::Core;
		};

		static void Initialize();
		static void Shutdown();
		static bool IsInitialized();

		static std::shared_ptr<spdlog::logger> GetCoreLogger();
		static std::shared_ptr<spdlog::logger> GetClientLogger();
		static std::shared_ptr<spdlog::logger> GetEditorConsoleLogger();

		static Event<const Entry&> OnLog;

		template <typename... Args>
		static void PrintMessage(const Type type, const Level level, fmt::format_string<Args...> format, Args&&... args) {
			if (!EnsureInitialized()) {
				return;
			}

			if (!ShouldLog(type, level)) {
				return;
			}

			const std::string message = fmt::format(format, std::forward<Args>(args)...);
			PrintMessage(type, level, std::string_view(message));
		}

		template <typename... Args>
		static void PrintMessageTag(const Type type, const Level level, std::string_view tag, fmt::format_string<Args...> format, Args&&... args) {
			if (!EnsureInitialized()) {
				return;
			}

			if (!ShouldLog(type, level)) {
				return;
			}

			const std::string message = fmt::format(format, std::forward<Args>(args)...);
			PrintMessageTag(type, level, tag, std::string_view(message));
		}

		static void PrintMessage(Type type, Level level, std::string_view message);
		static void PrintMessageTag(Type type, Level level, std::string_view tag, std::string_view message);

		static const char* LevelToString(Level level);

	private:
		static bool EnsureInitialized();
		static std::shared_ptr<spdlog::logger> SelectLogger(Type type);
		static spdlog::level::level_enum ToSpdlogLevel(Level level);
		static bool ShouldLog(Type type, Level level);
		static void Emit(std::shared_ptr<spdlog::logger>& logger, Level level, std::string_view message);

		inline static std::mutex s_StateMutex;
		inline static bool s_Initialized = false;
		inline static std::shared_ptr<spdlog::logger> s_CoreLogger;
		inline static std::shared_ptr<spdlog::logger> s_ClientLogger;
		inline static std::shared_ptr<spdlog::logger> s_EditorConsoleLogger;
	};

} // namespace Axiom

#define AIM_CORE_TRACE(...) ::Axiom::Log::PrintMessage(::Axiom::Log::Type::Core, ::Axiom::Log::Level::Trace, __VA_ARGS__)
#define AIM_CORE_INFO(...) ::Axiom::Log::PrintMessage(::Axiom::Log::Type::Core, ::Axiom::Log::Level::Info, __VA_ARGS__)
#define AIM_CORE_WARN(...) ::Axiom::Log::PrintMessage(::Axiom::Log::Type::Core, ::Axiom::Log::Level::Warn, __VA_ARGS__)
#define AIM_CORE_ERROR(...) ::Axiom::Log::PrintMessage(::Axiom::Log::Type::Core, ::Axiom::Log::Level::Error, __VA_ARGS__)
#define AIM_CORE_FATAL(...) ::Axiom::Log::PrintMessage(::Axiom::Log::Type::Core, ::Axiom::Log::Level::Critical, __VA_ARGS__)

#define AIM_TRACE(...) ::Axiom::Log::PrintMessage(::Axiom::Log::Type::Client, ::Axiom::Log::Level::Trace, __VA_ARGS__)
#define AIM_INFO(...) ::Axiom::Log::PrintMessage(::Axiom::Log::Type::Client, ::Axiom::Log::Level::Info, __VA_ARGS__)
#define AIM_WARN(...) ::Axiom::Log::PrintMessage(::Axiom::Log::Type::Client, ::Axiom::Log::Level::Warn, __VA_ARGS__)
#define AIM_ERROR(...) ::Axiom::Log::PrintMessage(::Axiom::Log::Type::Client, ::Axiom::Log::Level::Error, __VA_ARGS__)
#define AIM_FATAL(...) ::Axiom::Log::PrintMessage(::Axiom::Log::Type::Client, ::Axiom::Log::Level::Critical, __VA_ARGS__)

#define AIM_CORE_TRACE_TAG(tag, ...) ::Axiom::Log::PrintMessageTag(::Axiom::Log::Type::Core, ::Axiom::Log::Level::Trace, tag, __VA_ARGS__)
#define AIM_CORE_INFO_TAG(tag, ...) ::Axiom::Log::PrintMessageTag(::Axiom::Log::Type::Core, ::Axiom::Log::Level::Info, tag, __VA_ARGS__)
#define AIM_CORE_WARN_TAG(tag, ...) ::Axiom::Log::PrintMessageTag(::Axiom::Log::Type::Core, ::Axiom::Log::Level::Warn, tag, __VA_ARGS__)
#define AIM_CORE_ERROR_TAG(tag, ...) ::Axiom::Log::PrintMessageTag(::Axiom::Log::Type::Core, ::Axiom::Log::Level::Error, tag, __VA_ARGS__)
#define AIM_CORE_FATAL_TAG(tag, ...) ::Axiom::Log::PrintMessageTag(::Axiom::Log::Type::Core, ::Axiom::Log::Level::Critical, tag, __VA_ARGS__)

#define AIM_TRACE_TAG(tag, ...) ::Axiom::Log::PrintMessageTag(::Axiom::Log::Type::Client, ::Axiom::Log::Level::Trace, tag, __VA_ARGS__)
#define AIM_INFO_TAG(tag, ...) ::Axiom::Log::PrintMessageTag(::Axiom::Log::Type::Client, ::Axiom::Log::Level::Info, tag, __VA_ARGS__)
#define AIM_WARN_TAG(tag, ...) ::Axiom::Log::PrintMessageTag(::Axiom::Log::Type::Client, ::Axiom::Log::Level::Warn, tag, __VA_ARGS__)
#define AIM_ERROR_TAG(tag, ...) ::Axiom::Log::PrintMessageTag(::Axiom::Log::Type::Client, ::Axiom::Log::Level::Error, tag, __VA_ARGS__)
#define AIM_FATAL_TAG(tag, ...) ::Axiom::Log::PrintMessageTag(::Axiom::Log::Type::Client, ::Axiom::Log::Level::Critical, tag, __VA_ARGS__)

#define AIM_CONSOLE_LOG_TRACE(...) ::Axiom::Log::PrintMessage(::Axiom::Log::Type::EditorConsole, ::Axiom::Log::Level::Trace, __VA_ARGS__)
#define AIM_CONSOLE_LOG_INFO(...) ::Axiom::Log::PrintMessage(::Axiom::Log::Type::EditorConsole, ::Axiom::Log::Level::Info, __VA_ARGS__)
#define AIM_CONSOLE_LOG_WARN(...) ::Axiom::Log::PrintMessage(::Axiom::Log::Type::EditorConsole, ::Axiom::Log::Level::Warn, __VA_ARGS__)
#define AIM_CONSOLE_LOG_ERROR(...) ::Axiom::Log::PrintMessage(::Axiom::Log::Type::EditorConsole, ::Axiom::Log::Level::Error, __VA_ARGS__)
#define AIM_CONSOLE_LOG_FATAL(...) ::Axiom::Log::PrintMessage(::Axiom::Log::Type::EditorConsole, ::Axiom::Log::Level::Critical, __VA_ARGS__)
