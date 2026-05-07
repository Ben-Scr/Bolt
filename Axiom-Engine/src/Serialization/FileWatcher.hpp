#pragma once
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Axiom {

	class FileWatcher {
	public:
		using Callback = std::function<void()>;

		FileWatcher() = default;
		~FileWatcher();

		FileWatcher(const FileWatcher&) = delete;
		FileWatcher& operator=(const FileWatcher&) = delete;

		// `recursive` controls whether directory targets monitor their full
		// subtree. Defaults to true to preserve historical behavior. Pass
		// `false` to watch only the immediate children of each directory
		// target (file targets ignore the flag).
		void Watch(const std::string& path, const std::string& pattern, Callback callback, bool recursive = true);
		void Watch(const std::vector<std::string>& paths, const std::vector<std::string>& patterns, Callback callback, bool recursive = true);
		void Stop();
		void Poll(float pollIntervalSeconds = 1.0f);

		bool IsWatching() const { return m_Watching.load(); }

	private:
		struct WatchTarget {
			std::filesystem::path Path;
			bool IsDirectory = false;
			bool Recursive = true;
		};

		// Per-file metadata used for change detection. We record (mtime, size) instead of just
		// mtime so the save-via-rename pattern (write to temp + rename over original) — which
		// can produce identical mtimes when timer resolution is coarser than the operation —
		// still trips a snapshot diff once the size differs.
		struct FileFingerprint {
			std::filesystem::file_time_type WriteTime{};
			std::uintmax_t Size{ 0 };

			bool operator==(const FileFingerprint& other) const {
				return WriteTime == other.WriteTime && Size == other.Size;
			}

			bool operator!=(const FileFingerprint& other) const {
				return !(*this == other);
			}
		};

		using Snapshot = std::unordered_map<std::string, FileFingerprint>;

		void WorkerMain();
		bool WaitForNativeChanges();
		Snapshot BuildSnapshot() const;
		void ConfigurePatterns(const std::vector<std::string>& patterns);

		bool MatchesFile(const std::filesystem::path& filePath) const;
		bool ShouldIgnoreDirectory(const std::filesystem::path& directoryPath) const;

		static std::string NormalizeKey(const std::filesystem::path& path);
		static std::string ToLowerCopy(std::string value);
		static bool LooksLikeDirectory(const std::filesystem::path& path);

		std::vector<WatchTarget> m_Targets;
		std::unordered_set<std::string> m_Extensions;
		std::unordered_set<std::string> m_Filenames;
		Callback m_Callback;
		Snapshot m_FileTimestamps;
		std::thread m_Worker;
		std::condition_variable m_WakeCondition;
		std::mutex m_StateMutex;
		std::atomic<bool> m_Watching{ false };
		std::atomic<bool> m_PendingChanges{ false };
		std::atomic<int> m_PollIntervalMs{ 1000 };
		std::string m_WatchDescription;
	};

} // namespace Axiom
