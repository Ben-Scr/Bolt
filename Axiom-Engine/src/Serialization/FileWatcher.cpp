#include "pch.hpp"
#include "Serialization/FileWatcher.hpp"
#include "Core/Log.hpp"

#include <algorithm>
#include <cctype>

namespace Axiom {

	namespace {
		constexpr std::filesystem::directory_options kDirectoryOptions =
			std::filesystem::directory_options::skip_permission_denied;
		constexpr int kMaxIdlePollIntervalMs = 5000;
#ifdef AIM_PLATFORM_WINDOWS
		constexpr DWORD kNativeWatchTimeoutMs = 250;
#endif

		const std::unordered_set<std::string> kIgnoredDirectoryNames = {
			".git",
			".vs",
			".idea",
			"bin",
			"bin-int",
			"obj",
			"build",
			"out"
		};

		std::filesystem::path NormalizeWatchTargetPath(const std::filesystem::path& path) {
			std::error_code ec;
			if (std::filesystem::exists(path, ec)) {
				std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path, ec);
				if (!ec) {
					return canonicalPath.make_preferred();
				}
				ec.clear();
			}

			std::filesystem::path absolutePath = std::filesystem::absolute(path, ec);
			if (ec) {
				return path.lexically_normal().make_preferred();
			}

			return absolutePath.lexically_normal().make_preferred();
		}

		template <typename TSnapshot>
		bool HasSnapshotChanged(const TSnapshot& currentSnapshot, const TSnapshot& nextSnapshot) {
			if (nextSnapshot.size() != currentSnapshot.size()) {
				return true;
			}

			// Compare on the (mtime, size) tuple — relying on mtime alone misses the
			// save-via-rename pattern when filesystem timestamp granularity is coarser
			// than the rename window (common on FAT/NTFS for rapid editor saves).
			for (const auto& [path, fingerprint] : nextSnapshot) {
				const auto it = currentSnapshot.find(path);
				if (it == currentSnapshot.end() || it->second != fingerprint) {
					return true;
				}
			}

			return false;
		}
	}

	FileWatcher::~FileWatcher() {
		Stop();
	}

	void FileWatcher::Watch(const std::string& path, const std::string& pattern, Callback callback, bool recursive) {
		Watch(std::vector<std::string>{ path }, std::vector<std::string>{ pattern }, std::move(callback), recursive);
	}

	void FileWatcher::Watch(const std::vector<std::string>& paths, const std::vector<std::string>& patterns, Callback callback, bool recursive) {
		Stop();

		ConfigurePatterns(patterns);
		m_Callback = std::move(callback);
		m_Targets.clear();
		m_WatchDescription.clear();

		for (const std::string& pathString : paths) {
			if (pathString.empty()) {
				continue;
			}

			const std::filesystem::path path(pathString);
			std::error_code ec;
			const bool exists = std::filesystem::exists(path, ec);
			const bool isDirectory = exists ? std::filesystem::is_directory(path, ec) : LooksLikeDirectory(path);
			m_Targets.push_back({ NormalizeWatchTargetPath(path), isDirectory, isDirectory && recursive });
		}

		if (m_Targets.empty() || !m_Callback) {
			m_Callback = nullptr;
			return;
		}

		m_FileTimestamps = BuildSnapshot();
		m_PendingChanges.store(false);
		m_Watching.store(true);
		m_WatchDescription = m_Targets.size() == 1
			? m_Targets.front().Path.string()
			: std::to_string(m_Targets.size()) + " targets";

		m_Worker = std::thread(&FileWatcher::WorkerMain, this);
		AIM_CORE_INFO_TAG("FileWatcher", "Watching {} for {} pattern(s)", m_WatchDescription, patterns.size());
	}

	void FileWatcher::Stop() {
		m_Watching.store(false);
		m_WakeCondition.notify_all();

		if (m_Worker.joinable()) {
			m_Worker.join();
		}

		m_Targets.clear();
		m_Extensions.clear();
		m_Filenames.clear();
		m_FileTimestamps.clear();
		m_Callback = nullptr;
		m_PendingChanges.store(false);
		m_WatchDescription.clear();
	}

	void FileWatcher::Poll(float pollIntervalSeconds) {
		const int pollIntervalMs = std::max(50, static_cast<int>(pollIntervalSeconds * 1000.0f));
		m_PollIntervalMs.store(pollIntervalMs);

		if (!m_PendingChanges.exchange(false) || !m_Callback) {
			return;
		}

		AIM_CORE_INFO_TAG("FileWatcher", "Changes detected in {}", m_WatchDescription);
		m_Callback();
	}

	void FileWatcher::WorkerMain() {
#ifdef AIM_PLATFORM_WINDOWS
		if (WaitForNativeChanges()) {
			return;
		}
#endif

		int idlePollIntervalMs = m_PollIntervalMs.load();

		while (m_Watching.load()) {
			std::unique_lock<std::mutex> lock(m_StateMutex);
			m_WakeCondition.wait_for(lock, std::chrono::milliseconds(idlePollIntervalMs), [this]() {
				return !m_Watching.load();
			});
			lock.unlock();

			if (!m_Watching.load()) {
				break;
			}

			Snapshot nextSnapshot = BuildSnapshot();
			const bool changed = HasSnapshotChanged(m_FileTimestamps, nextSnapshot);

			m_FileTimestamps = std::move(nextSnapshot);
			if (changed) {
				m_PendingChanges.store(true);
				idlePollIntervalMs = m_PollIntervalMs.load();
			}
			else {
				idlePollIntervalMs = std::min(kMaxIdlePollIntervalMs, std::max(m_PollIntervalMs.load(), idlePollIntervalMs * 2));
			}
		}
	}

	bool FileWatcher::WaitForNativeChanges() {
#ifndef AIM_PLATFORM_WINDOWS
		return false;
#else
		struct NativeWatchRoot {
			std::filesystem::path Path;
			bool Recursive = false;
		};

		std::unordered_map<std::string, NativeWatchRoot> rootsByKey;
		for (const WatchTarget& target : m_Targets) {
			std::filesystem::path rootPath = target.IsDirectory ? target.Path : target.Path.parent_path();
			if (rootPath.empty()) {
				continue;
			}

			std::error_code ec;
			if (!std::filesystem::exists(rootPath, ec) || !std::filesystem::is_directory(rootPath, ec)) {
				continue;
			}

			// File targets watch their parent dir non-recursively (we only
			// care about the file itself); directory targets honor the
			// caller-supplied Recursive flag.
			const bool wantRecursive = target.IsDirectory && target.Recursive;
			NativeWatchRoot root{ NormalizeWatchTargetPath(rootPath), wantRecursive };
			const std::string key = NormalizeKey(root.Path);
			auto [it, inserted] = rootsByKey.emplace(key, root);
			if (!inserted) {
				it->second.Recursive = it->second.Recursive || root.Recursive;
			}
		}

		if (rootsByKey.empty()) {
			return false;
		}

		if (rootsByKey.size() > MAXIMUM_WAIT_OBJECTS) {
			AIM_CORE_WARN_TAG("FileWatcher", "Watching {} exceeds the Windows notification handle limit; falling back to polling", m_WatchDescription);
			return false;
		}

		std::vector<HANDLE> handles;
		handles.reserve(rootsByKey.size());

		for (const auto& [key, root] : rootsByKey) {
			(void)key;

			const HANDLE handle = FindFirstChangeNotificationW(
				root.Path.c_str(),
				root.Recursive ? TRUE : FALSE,
				FILE_NOTIFY_CHANGE_FILE_NAME |
				FILE_NOTIFY_CHANGE_DIR_NAME |
				FILE_NOTIFY_CHANGE_LAST_WRITE |
				FILE_NOTIFY_CHANGE_CREATION |
				FILE_NOTIFY_CHANGE_SIZE);

			if (handle == INVALID_HANDLE_VALUE) {
				AIM_CORE_WARN_TAG("FileWatcher", "Failed to create a native watch for {}; falling back to polling", root.Path.string());
				for (HANDLE openHandle : handles) {
					FindCloseChangeNotification(openHandle);
				}
				return false;
			}

			handles.push_back(handle);
		}

		struct NotificationHandleCloser {
			std::vector<HANDLE>& Handles;

			~NotificationHandleCloser() {
				for (HANDLE handle : Handles) {
					if (handle != INVALID_HANDLE_VALUE) {
						FindCloseChangeNotification(handle);
					}
				}
			}
		} handleCloser{ handles };

		while (m_Watching.load()) {
			const DWORD waitResult = WaitForMultipleObjects(
				static_cast<DWORD>(handles.size()),
				handles.data(),
				FALSE,
				kNativeWatchTimeoutMs);

			if (waitResult == WAIT_TIMEOUT) {
				continue;
			}

			if (waitResult >= WAIT_OBJECT_0 && waitResult < WAIT_OBJECT_0 + handles.size()) {
				Snapshot nextSnapshot = BuildSnapshot();
				const bool changed = HasSnapshotChanged(m_FileTimestamps, nextSnapshot);
				m_FileTimestamps = std::move(nextSnapshot);
				if (changed) {
					m_PendingChanges.store(true);
				}

				const HANDLE changedHandle = handles[waitResult - WAIT_OBJECT_0];
				if (!FindNextChangeNotification(changedHandle)) {
					AIM_CORE_WARN_TAG("FileWatcher", "Native notifications failed for {}; falling back to polling", m_WatchDescription);
					return false;
				}

				continue;
			}

			AIM_CORE_WARN_TAG("FileWatcher", "Native notifications failed for {}; falling back to polling", m_WatchDescription);
			return false;
		}

		return true;
#endif
	}

	FileWatcher::Snapshot FileWatcher::BuildSnapshot() const {
		Snapshot snapshot;

		auto recordFile = [&](const std::filesystem::directory_entry& entry, std::error_code& ec) {
			if (!entry.is_regular_file(ec) || ec || !MatchesFile(entry.path())) {
				ec.clear();
				return;
			}
			const std::string key = NormalizeKey(entry.path());
			const std::filesystem::file_time_type writeTime = entry.last_write_time(ec);
			if (ec) {
				ec.clear();
				return;
			}
			std::error_code sizeEc;
			const std::uintmax_t size = entry.file_size(sizeEc);
			snapshot[key] = FileFingerprint{ writeTime, sizeEc ? std::uintmax_t{ 0 } : size };
			ec.clear();
		};

		for (const WatchTarget& target : m_Targets) {
			std::error_code ec;
			if (target.IsDirectory) {
				if (!std::filesystem::exists(target.Path, ec) || !std::filesystem::is_directory(target.Path, ec)) {
					continue;
				}

				if (target.Recursive) {
					for (std::filesystem::recursive_directory_iterator it(target.Path, kDirectoryOptions, ec), end;
						 it != end;
						 it.increment(ec)) {
						if (ec) {
							ec.clear();
							continue;
						}

						if (it->is_directory(ec)) {
							if (!ec && ShouldIgnoreDirectory(it->path())) {
								it.disable_recursion_pending();
							}
							ec.clear();
							continue;
						}

						recordFile(*it, ec);
					}
				} else {
					for (std::filesystem::directory_iterator it(target.Path, kDirectoryOptions, ec), end;
						 it != end;
						 it.increment(ec)) {
						if (ec) {
							ec.clear();
							continue;
						}
						if (it->is_directory(ec)) {
							ec.clear();
							continue;
						}
						recordFile(*it, ec);
					}
				}

				continue;
			}

			if (!std::filesystem::exists(target.Path, ec) || !std::filesystem::is_regular_file(target.Path, ec) || !MatchesFile(target.Path)) {
				continue;
			}

			const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(target.Path, ec);
			if (ec) {
				continue;
			}
			std::error_code sizeEc;
			const std::uintmax_t size = std::filesystem::file_size(target.Path, sizeEc);
			snapshot[NormalizeKey(target.Path)] = FileFingerprint{ writeTime, sizeEc ? std::uintmax_t{ 0 } : size };
		}

		return snapshot;
	}

	void FileWatcher::ConfigurePatterns(const std::vector<std::string>& patterns) {
		m_Extensions.clear();
		m_Filenames.clear();

		for (const std::string& pattern : patterns) {
			if (pattern.empty()) {
				continue;
			}

			const std::string normalizedPattern = ToLowerCopy(pattern);
			if (!normalizedPattern.empty() && normalizedPattern.front() == '.') {
				m_Extensions.insert(normalizedPattern);
			}
			else {
				m_Filenames.insert(normalizedPattern);
			}
		}
	}

	bool FileWatcher::MatchesFile(const std::filesystem::path& filePath) const {
		if (m_Extensions.empty() && m_Filenames.empty()) {
			return true;
		}

		const std::string fileName = ToLowerCopy(filePath.filename().string());
		if (m_Filenames.contains(fileName)) {
			return true;
		}

		const std::string extension = ToLowerCopy(filePath.extension().string());
		return !extension.empty() && m_Extensions.contains(extension);
	}

	bool FileWatcher::ShouldIgnoreDirectory(const std::filesystem::path& directoryPath) const {
		return kIgnoredDirectoryNames.contains(ToLowerCopy(directoryPath.filename().string()));
	}

	std::string FileWatcher::NormalizeKey(const std::filesystem::path& path) {
		std::error_code ec;
		std::filesystem::path normalized = std::filesystem::absolute(path, ec);
		if (ec) {
			normalized = path.lexically_normal();
		}
		else {
			normalized = normalized.lexically_normal();
		}

		return normalized.make_preferred().string();
	}

	std::string FileWatcher::ToLowerCopy(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return value;
	}

	bool FileWatcher::LooksLikeDirectory(const std::filesystem::path& path) {
		const std::string native = path.lexically_normal().make_preferred().string();
		if (!native.empty()) {
			const char lastCharacter = native.back();
			if (lastCharacter == '/' || lastCharacter == '\\') {
				return true;
			}
		}

		return !path.has_extension();
	}

} // namespace Axiom
