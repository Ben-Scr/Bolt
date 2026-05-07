#include "pch.hpp"
#include "Utils/Process.hpp"

#include <cctype>
#include <sstream>

#ifdef AIM_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <algorithm>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
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

	Result Run(const std::vector<std::string>& command, const std::filesystem::path& workingDirectory, std::chrono::milliseconds timeout) {
		Result result;
		if (command.empty()) {
			result.Output = "No command specified";
			return result;
		}

		const bool hasTimeout = timeout.count() > 0;

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

		// Drain the pipe with a deadline. PeekNamedPipe lets us check for
		// data without blocking; if no data and the deadline expires, we
		// terminate the child instead of blocking forever.
		const auto deadline = hasTimeout ? std::chrono::steady_clock::now() + timeout
		                                  : std::chrono::steady_clock::time_point::max();
		char buffer[4096];
		while (true) {
			if (hasTimeout && std::chrono::steady_clock::now() >= deadline) {
				result.TimedOut = true;
				TerminateProcess(processInfo.hProcess, static_cast<UINT>(-1));
				break;
			}

			DWORD available = 0;
			if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr)) {
				break; // pipe closed (child exited and write end gone)
			}
			if (available == 0) {
				// Either the child is still running with no output, or it has
				// exited. Wait briefly for either an output byte or process
				// exit before retrying.
				const DWORD waitMs = hasTimeout
					? static_cast<DWORD>(std::min<long long>(50,
						std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count()))
					: 50;
				const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, waitMs == 0 ? 1 : waitMs);
				if (waitResult == WAIT_OBJECT_0) {
					// Process exited — drain any final bytes still in the pipe.
					DWORD finalAvailable = 0;
					if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &finalAvailable, nullptr) && finalAvailable > 0) {
						DWORD bytesRead = 0;
						while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
							result.Output.append(buffer, buffer + bytesRead);
						}
					}
					break;
				}
				continue;
			}

			DWORD bytesRead = 0;
			if (!ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) || bytesRead == 0) {
				break;
			}
			result.Output.append(buffer, buffer + bytesRead);
		}
		CloseHandle(readPipe);

		// Final wait — bounded by whatever's left of the caller-supplied timeout
		// instead of INFINITE: the read loop can break on PeekNamedPipe failing
		// (broken pipe, e.g. the child closed stdout but is still running), so
		// an unbounded wait here would hang the editor on a misbehaving child.
		DWORD finalWaitMs = INFINITE;
		if (result.TimedOut) {
			finalWaitMs = 5000;
		}
		else if (hasTimeout) {
			const auto remaining = deadline - std::chrono::steady_clock::now();
			const long long remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
			finalWaitMs = remainingMs > 0 ? static_cast<DWORD>(std::min<long long>(remainingMs, 0x7FFFFFFF)) : 0;
		}
		const DWORD finalWaitResult = WaitForSingleObject(processInfo.hProcess, finalWaitMs);
		if (finalWaitResult != WAIT_OBJECT_0 && hasTimeout) {
			// Remaining timeout elapsed before the process exited — terminate so
			// we don't leak a zombie child handle past the caller's deadline.
			result.TimedOut = true;
			TerminateProcess(processInfo.hProcess, static_cast<UINT>(-1));
			WaitForSingleObject(processInfo.hProcess, 5000);
		}
		DWORD exitCode = static_cast<DWORD>(-1);
		GetExitCodeProcess(processInfo.hProcess, &exitCode);
		result.ExitCode = static_cast<int>(exitCode);

		CloseHandle(processInfo.hProcess);
		CloseHandle(processInfo.hThread);
		return result;
#else
		// Build the argv array BEFORE fork(). Between fork() and execvp() we
		// can only call async-signal-safe functions; std::vector::push_back
		// allocates via the global allocator, which is not async-signal-safe
		// in a multithreaded program (a thread holding malloc's lock at fork
		// time leaves it locked in the child, deadlocking the next malloc).
		// Pre-built argv keeps the post-fork path to chdir/dup2/close/execvp.
		std::vector<char*> argv;
		argv.reserve(command.size() + 1);
		for (const std::string& arg : command) {
			argv.push_back(const_cast<char*>(arg.c_str()));
		}
		argv.push_back(nullptr);

		const std::string workingDirectoryStr = workingDirectory.empty() ? std::string() : workingDirectory.string();
		const char* workingDirectoryCStr = workingDirectoryStr.empty() ? nullptr : workingDirectoryStr.c_str();

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
			if (workingDirectoryCStr) {
				if (chdir(workingDirectoryCStr) != 0) {
					_exit(126); // distinct from execvp's 127
				}
			}

			dup2(pipeFd[1], STDOUT_FILENO);
			dup2(pipeFd[1], STDERR_FILENO);
			close(pipeFd[0]);
			close(pipeFd[1]);

			execvp(argv[0], argv.data());
			_exit(127);
		}

		close(pipeFd[1]);

		const auto deadline = hasTimeout ? std::chrono::steady_clock::now() + timeout
		                                  : std::chrono::steady_clock::time_point::max();
		char buffer[4096];
		for (;;) {
			int waitMs = -1; // -1 = block forever
			if (hasTimeout) {
				const auto now = std::chrono::steady_clock::now();
				if (now >= deadline) {
					result.TimedOut = true;
					kill(pid, SIGKILL);
					break;
				}
				waitMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
			}

			pollfd pfd{ pipeFd[0], POLLIN, 0 };
			const int pollResult = poll(&pfd, 1, waitMs);
			if (pollResult < 0) {
				if (errno == EINTR) continue;
				break;
			}
			if (pollResult == 0) {
				// poll timed out — by construction this only happens when we
				// hit the deadline.
				result.TimedOut = true;
				kill(pid, SIGKILL);
				break;
			}

			ssize_t bytesRead = read(pipeFd[0], buffer, sizeof(buffer));
			if (bytesRead > 0) {
				result.Output.append(buffer, buffer + bytesRead);
				continue;
			}
			if (bytesRead == 0) {
				break; // EOF (child closed write end)
			}
			if (errno == EINTR) continue;
			break;
		}
		close(pipeFd[0]);

		int status = 0;
		while (waitpid(pid, &status, 0) < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (WIFEXITED(status)) {
			result.ExitCode = WEXITSTATUS(status);
		} else if (WIFSIGNALED(status)) {
			result.ExitCode = 128 + WTERMSIG(status); // shell convention
		} else {
			result.ExitCode = -1;
		}
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
