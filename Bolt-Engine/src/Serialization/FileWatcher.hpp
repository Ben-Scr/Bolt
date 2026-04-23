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

namespace Bolt {

	class FileWatcher {
	public:
		using Callback = std::function<void()>;

		FileWatcher() = default;
		~FileWatcher();

		FileWatcher(const FileWatcher&) = delete;
		FileWatcher& operator=(const FileWatcher&) = delete;

		void Watch(const std::string& path, const std::string& pattern, Callback callback);
		void Watch(const std::vector<std::string>& paths, const std::vector<std::string>& patterns, Callback callback);
		void Stop();
		void Poll(float pollIntervalSeconds = 1.0f);

		bool IsWatching() const { return m_Watching.load(); }

	private:
		struct WatchTarget {
			std::filesystem::path Path;
			bool IsDirectory = false;
		};

		using Snapshot = std::unordered_map<std::string, std::filesystem::file_time_type>;

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

} // namespace Bolt
