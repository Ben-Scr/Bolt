#pragma once
#include "Core/Base.hpp"
#include "Core/Application.hpp"

#include <memory>
#include <string>
#include <vector>

#ifdef AIM_PLATFORM_WINDOWS
#include <cwchar>
#include <shellapi.h>
#endif

extern Axiom::Application* CreateApplication();
inline bool g_ApplicationRunning = true;

namespace Axiom {

#ifdef AIM_PLATFORM_WINDOWS
	namespace Detail {
		class CommandLineStorage {
		public:
			void SetFromNarrowArgs(int argc, char** argv) {
				m_Args.clear();
				m_Args.reserve(argc > 0 ? static_cast<size_t>(argc) : 0);

				for (int i = 0; i < argc; ++i) {
					m_Args.emplace_back((argv && argv[i]) ? argv[i] : "");
				}

				RefreshPointers();
			}

			bool SetFromWindowsCommandLine() {
				int argc = 0;
				LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
				if (!argv) {
					return false;
				}

				m_Args.clear();
				m_Args.reserve(argc > 0 ? static_cast<size_t>(argc) : 0);
				for (int i = 0; i < argc; ++i) {
					m_Args.push_back(WideToUtf8(argv[i]));
				}

				LocalFree(argv);
				RefreshPointers();
				return true;
			}

			int Count() const { return static_cast<int>(m_ArgPointers.size()); }
			char** Data() { return m_ArgPointers.empty() ? nullptr : m_ArgPointers.data(); }

		private:
			static std::string WideToUtf8(const wchar_t* value) {
				if (!value || *value == L'\0') {
					return {};
				}

				const int wideLength = static_cast<int>(std::wcslen(value));
				const int requiredBytes = WideCharToMultiByte(
					CP_UTF8, WC_ERR_INVALID_CHARS,
					value, wideLength,
					nullptr, 0, nullptr, nullptr);
				if (requiredBytes <= 0) {
					return {};
				}

				std::string utf8(static_cast<size_t>(requiredBytes), '\0');
				if (WideCharToMultiByte(
					CP_UTF8, WC_ERR_INVALID_CHARS,
					value, wideLength,
					utf8.data(), requiredBytes, nullptr, nullptr) != requiredBytes) {
					return {};
				}

				return utf8;
			}

			void RefreshPointers() {
				m_ArgPointers.clear();
				m_ArgPointers.reserve(m_Args.size());
				for (std::string& arg : m_Args) {
					m_ArgPointers.push_back(arg.data());
				}
			}

			std::vector<std::string> m_Args;
			std::vector<char*> m_ArgPointers;
		};

		inline CommandLineStorage& GetCommandLineStorage() {
			static CommandLineStorage storage;
			return storage;
		}
	}
#endif

	inline int Main(int argc, char** argv) {
		InitializeCore();
		struct CoreShutdownGuard {
			~CoreShutdownGuard() {
				ShutdownCore();
			}
		} shutdownGuard;

#ifdef AIM_PLATFORM_WINDOWS
		Detail::CommandLineStorage& commandLineStorage = Detail::GetCommandLineStorage();
		if (!commandLineStorage.SetFromWindowsCommandLine()) {
			commandLineStorage.SetFromNarrowArgs(argc, argv);
		}
		Application::SetCommandLineArgs(commandLineStorage.Count(), commandLineStorage.Data());
#else
		Application::SetCommandLineArgs(argc, argv);
#endif

		try {
			std::unique_ptr<Application> app(Axiom::CreateApplication());
			AIM_CORE_ASSERT(app, "Client app is null!");

			app->Run();
			return 0;
		}
		catch (const std::exception& ex) {
			AIM_CORE_ERROR("Unhandled exception at app boundary: {}", ex.what());
			return -1;
		}
		catch (...) {
			AIM_CORE_ERROR("Unhandled non-std exception at app boundary");
			return -1;
		}
	}
}

#if AIM_DIST && AIM_PLATFORM_WINDOWS

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow)
{
	return Axiom::Main(__argc, __argv);
}

#else

int main(int argc, char** argv)
{
	return Axiom::Main(argc, argv);
}

#endif
