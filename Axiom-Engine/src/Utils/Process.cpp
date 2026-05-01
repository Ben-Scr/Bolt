#include "pch.hpp"
#include "Utils/Process.hpp"

#include <cctype>
#include <sstream>

#ifdef AIM_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Axiom::Process {

	namespace {
		std::string QuoteForShell(const std::string& arg) {
			if (arg.empty()) {
				return "\"\"";
			}

			bool needsQuotes = false;
			for (const char ch : arg) {
				if (std::isspace(static_cast<unsigned char>(ch)) || ch == '"' || ch == '\\') {
					needsQuotes = true;
					break;
				}
			}

			if (!needsQuotes) {
				return arg;
			}

			std::string escaped = "\"";
			size_t backslashes = 0;
			for (const char ch : arg) {
				if (ch == '\\') {
					backslashes++;
					continue;
				}

				if (ch == '"') {
					escaped.append(backslashes * 2 + 1, '\\');
					escaped.push_back('"');
					backslashes = 0;
					continue;
				}

				escaped.append(backslashes, '\\');
				backslashes = 0;
				escaped.push_back(ch);
			}

			escaped.append(backslashes * 2, '\\');
			escaped.push_back('"');
			return escaped;
		}

		std::string BuildCommandLine(const std::vector<std::string>& command) {
			std::ostringstream stream;
			for (size_t i = 0; i < command.size(); i++) {
				if (i > 0) {
					stream << ' ';
				}
				stream << QuoteForShell(command[i]);
			}
			return stream.str();
		}

#ifdef AIM_PLATFORM_WINDOWS
		std::wstring ToWide(const std::string& value) {
			if (value.empty()) {
				return {};
			}

			const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
			if (size <= 0) {
				return {};
			}

			std::wstring wide(static_cast<size_t>(size), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), size);
			if (!wide.empty() && wide.back() == L'\0') {
				wide.pop_back();
			}
			return wide;
		}

		std::string ReadPipeToEnd(HANDLE readPipe) {
			std::string output;
			char buffer[4096];
			DWORD bytesRead = 0;

			while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
				output.append(buffer, buffer + bytesRead);
			}

			return output;
		}
#endif
	} // namespace

	Result Run(const std::vector<std::string>& command, const std::filesystem::path& workingDirectory) {
		Result result;
		if (command.empty()) {
			result.Output = "No command specified";
			return result;
		}

#ifdef AIM_PLATFORM_WINDOWS
		SECURITY_ATTRIBUTES securityAttributes{};
		securityAttributes.nLength = sizeof(securityAttributes);
		securityAttributes.bInheritHandle = TRUE;

		HANDLE readPipe = nullptr;
		HANDLE writePipe = nullptr;
		if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0)) {
			result.Output = "Failed to create process output pipe";
			return result;
		}

		SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

		const std::string commandLineUtf8 = BuildCommandLine(command);
		std::wstring commandLine = ToWide(commandLineUtf8);
		std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
		mutableCommandLine.push_back(L'\0');

		std::wstring workingDirectoryWide = workingDirectory.empty()
			? std::wstring()
			: ToWide(workingDirectory.string());

		STARTUPINFOW startupInfo{};
		startupInfo.cb = sizeof(startupInfo);
		startupInfo.dwFlags = STARTF_USESTDHANDLES;
		startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		startupInfo.hStdOutput = writePipe;
		startupInfo.hStdError = writePipe;

		PROCESS_INFORMATION processInfo{};
		const BOOL created = CreateProcessW(
			nullptr,
			mutableCommandLine.data(),
			nullptr,
			nullptr,
			TRUE,
			CREATE_NO_WINDOW,
			nullptr,
			workingDirectoryWide.empty() ? nullptr : workingDirectoryWide.c_str(),
			&startupInfo,
			&processInfo);

		CloseHandle(writePipe);

		if (!created) {
			const DWORD error = GetLastError();
			CloseHandle(readPipe);
			std::ostringstream stream;
			stream << "CreateProcessW failed (" << error << ") for command: " << commandLineUtf8;
			result.Output = stream.str();
			return result;
		}

		result.Output = ReadPipeToEnd(readPipe);
		CloseHandle(readPipe);

		WaitForSingleObject(processInfo.hProcess, INFINITE);
		DWORD exitCode = static_cast<DWORD>(-1);
		GetExitCodeProcess(processInfo.hProcess, &exitCode);
		result.ExitCode = static_cast<int>(exitCode);

		CloseHandle(processInfo.hProcess);
		CloseHandle(processInfo.hThread);
		return result;
#else
		int pipeFd[2];
		if (pipe(pipeFd) != 0) {
			result.Output = "Failed to create process output pipe";
			return result;
		}

		pid_t pid = fork();
		if (pid < 0) {
			close(pipeFd[0]);
			close(pipeFd[1]);
			result.Output = "fork() failed while launching process";
			return result;
		}

		if (pid == 0) {
			if (!workingDirectory.empty()) {
				chdir(workingDirectory.c_str());
			}

			dup2(pipeFd[1], STDOUT_FILENO);
			dup2(pipeFd[1], STDERR_FILENO);
			close(pipeFd[0]);
			close(pipeFd[1]);

			std::vector<char*> argv;
			argv.reserve(command.size() + 1);
			for (const std::string& arg : command) {
				argv.push_back(const_cast<char*>(arg.c_str()));
			}
			argv.push_back(nullptr);

			execvp(command[0].c_str(), argv.data());
			_exit(127);
		}

		close(pipeFd[1]);

		char buffer[4096];
		ssize_t bytesRead = 0;
		while ((bytesRead = read(pipeFd[0], buffer, sizeof(buffer))) > 0) {
			result.Output.append(buffer, buffer + bytesRead);
		}
		close(pipeFd[0]);

		int status = 0;
		waitpid(pid, &status, 0);
		result.ExitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
		return result;
#endif
	}

	bool LaunchDetached(const std::vector<std::string>& command, const std::filesystem::path& workingDirectory) {
		if (command.empty()) {
			return false;
		}

#ifdef AIM_PLATFORM_WINDOWS
		const std::string commandLineUtf8 = BuildCommandLine(command);
		std::wstring commandLine = ToWide(commandLineUtf8);
		std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
		mutableCommandLine.push_back(L'\0');

		std::wstring workingDirectoryWide = workingDirectory.empty()
			? std::wstring()
			: ToWide(workingDirectory.string());

		STARTUPINFOW startupInfo{};
		startupInfo.cb = sizeof(startupInfo);

		PROCESS_INFORMATION processInfo{};
		const BOOL created = CreateProcessW(
			nullptr,
			mutableCommandLine.data(),
			nullptr,
			nullptr,
			FALSE,
			CREATE_NEW_PROCESS_GROUP,
			nullptr,
			workingDirectoryWide.empty() ? nullptr : workingDirectoryWide.c_str(),
			&startupInfo,
			&processInfo);

		if (!created) {
			return false;
		}

		CloseHandle(processInfo.hProcess);
		CloseHandle(processInfo.hThread);
		return true;
#else
		pid_t pid = fork();
		if (pid < 0) {
			return false;
		}

		if (pid == 0) {
			if (!workingDirectory.empty()) {
				chdir(workingDirectory.c_str());
			}

			std::vector<char*> argv;
			argv.reserve(command.size() + 1);
			for (const std::string& arg : command) {
				argv.push_back(const_cast<char*>(arg.c_str()));
			}
			argv.push_back(nullptr);

			setsid();
			execvp(command[0].c_str(), argv.data());
			_exit(127);
		}

		return true;
#endif
	}

} // namespace Axiom::Process
