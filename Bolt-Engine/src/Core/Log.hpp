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

namespace Bolt {

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

} // namespace Bolt

#define BT_CORE_TRACE(...) ::Bolt::Log::PrintMessage(::Bolt::Log::Type::Core, ::Bolt::Log::Level::Trace, __VA_ARGS__)
#define BT_CORE_INFO(...) ::Bolt::Log::PrintMessage(::Bolt::Log::Type::Core, ::Bolt::Log::Level::Info, __VA_ARGS__)
#define BT_CORE_WARN(...) ::Bolt::Log::PrintMessage(::Bolt::Log::Type::Core, ::Bolt::Log::Level::Warn, __VA_ARGS__)
#define BT_CORE_ERROR(...) ::Bolt::Log::PrintMessage(::Bolt::Log::Type::Core, ::Bolt::Log::Level::Error, __VA_ARGS__)
#define BT_CORE_FATAL(...) ::Bolt::Log::PrintMessage(::Bolt::Log::Type::Core, ::Bolt::Log::Level::Critical, __VA_ARGS__)

#define BT_TRACE(...) ::Bolt::Log::PrintMessage(::Bolt::Log::Type::Client, ::Bolt::Log::Level::Trace, __VA_ARGS__)
#define BT_INFO(...) ::Bolt::Log::PrintMessage(::Bolt::Log::Type::Client, ::Bolt::Log::Level::Info, __VA_ARGS__)
#define BT_WARN(...) ::Bolt::Log::PrintMessage(::Bolt::Log::Type::Client, ::Bolt::Log::Level::Warn, __VA_ARGS__)
#define BT_ERROR(...) ::Bolt::Log::PrintMessage(::Bolt::Log::Type::Client, ::Bolt::Log::Level::Error, __VA_ARGS__)
#define BT_FATAL(...) ::Bolt::Log::PrintMessage(::Bolt::Log::Type::Client, ::Bolt::Log::Level::Critical, __VA_ARGS__)

#define BT_CORE_TRACE_TAG(tag, ...) ::Bolt::Log::PrintMessageTag(::Bolt::Log::Type::Core, ::Bolt::Log::Level::Trace, tag, __VA_ARGS__)
#define BT_CORE_INFO_TAG(tag, ...) ::Bolt::Log::PrintMessageTag(::Bolt::Log::Type::Core, ::Bolt::Log::Level::Info, tag, __VA_ARGS__)
#define BT_CORE_WARN_TAG(tag, ...) ::Bolt::Log::PrintMessageTag(::Bolt::Log::Type::Core, ::Bolt::Log::Level::Warn, tag, __VA_ARGS__)
#define BT_CORE_ERROR_TAG(tag, ...) ::Bolt::Log::PrintMessageTag(::Bolt::Log::Type::Core, ::Bolt::Log::Level::Error, tag, __VA_ARGS__)
#define BT_CORE_FATAL_TAG(tag, ...) ::Bolt::Log::PrintMessageTag(::Bolt::Log::Type::Core, ::Bolt::Log::Level::Critical, tag, __VA_ARGS__)

#define BT_TRACE_TAG(tag, ...) ::Bolt::Log::PrintMessageTag(::Bolt::Log::Type::Client, ::Bolt::Log::Level::Trace, tag, __VA_ARGS__)
#define BT_INFO_TAG(tag, ...) ::Bolt::Log::PrintMessageTag(::Bolt::Log::Type::Client, ::Bolt::Log::Level::Info, tag, __VA_ARGS__)
#define BT_WARN_TAG(tag, ...) ::Bolt::Log::PrintMessageTag(::Bolt::Log::Type::Client, ::Bolt::Log::Level::Warn, tag, __VA_ARGS__)
#define BT_ERROR_TAG(tag, ...) ::Bolt::Log::PrintMessageTag(::Bolt::Log::Type::Client, ::Bolt::Log::Level::Error, tag, __VA_ARGS__)
#define BT_FATAL_TAG(tag, ...) ::Bolt::Log::PrintMessageTag(::Bolt::Log::Type::Client, ::Bolt::Log::Level::Critical, tag, __VA_ARGS__)

#define BT_CONSOLE_LOG_TRACE(...) ::Bolt::Log::PrintMessage(::Bolt::Log::Type::EditorConsole, ::Bolt::Log::Level::Trace, __VA_ARGS__)
#define BT_CONSOLE_LOG_INFO(...) ::Bolt::Log::PrintMessage(::Bolt::Log::Type::EditorConsole, ::Bolt::Log::Level::Info, __VA_ARGS__)
#define BT_CONSOLE_LOG_WARN(...) ::Bolt::Log::PrintMessage(::Bolt::Log::Type::EditorConsole, ::Bolt::Log::Level::Warn, __VA_ARGS__)
#define BT_CONSOLE_LOG_ERROR(...) ::Bolt::Log::PrintMessage(::Bolt::Log::Type::EditorConsole, ::Bolt::Log::Level::Error, __VA_ARGS__)
#define BT_CONSOLE_LOG_FATAL(...) ::Bolt::Log::PrintMessage(::Bolt::Log::Type::EditorConsole, ::Bolt::Log::Level::Critical, __VA_ARGS__)
