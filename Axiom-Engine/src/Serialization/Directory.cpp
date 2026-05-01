#include <pch.hpp>
#include "Assets/AssetRegistry.hpp"
#include "Directory.hpp"
#include <filesystem>

namespace Axiom {

	void Directory::Create(const std::string& dir, bool recursive) {
		if (recursive)
			std::filesystem::create_directories(dir);
		else
			std::filesystem::create_directory(dir);
	}

	bool Directory::Exists(const std::string& dir) {
		std::error_code ec;
		return std::filesystem::exists(dir, ec) && std::filesystem::is_directory(dir, ec);
	}

	bool Directory::Delete(const std::string& path) {
		std::error_code ec;
		if (!std::filesystem::exists(path, ec) || ec)
			return false;

		if (std::filesystem::is_directory(path, ec))
			return std::filesystem::remove_all(path, ec) > 0 && !ec;

		const bool removed = std::filesystem::remove(path, ec) && !ec;
		if (removed) {
			AssetRegistry::DeleteCompanionMetadata(path);
		}
		return removed;
	}

	bool Directory::Move(const std::string& from, const std::string& to) {
		std::error_code ec;
		if (!std::filesystem::exists(from, ec) || ec)
			return false;

		std::filesystem::path dest(to);
		if (std::filesystem::is_directory(dest, ec)) {
			dest /= std::filesystem::path(from).filename();
		}

		std::filesystem::rename(from, dest, ec);
		if (!ec && std::filesystem::is_regular_file(dest, ec)) {
			AssetRegistry::MoveCompanionMetadata(from, dest.string());
		}
		return !ec;
	}

	bool Directory::Rename(const std::string& path, const std::string& newName) {
		std::error_code ec;
		if (!std::filesystem::exists(path, ec) || ec)
			return false;

		std::filesystem::path p(path);
		std::filesystem::path target = p.parent_path() / newName;
		std::filesystem::rename(p, target, ec);
		if (!ec && std::filesystem::is_regular_file(target, ec)) {
			AssetRegistry::MoveCompanionMetadata(path, target.string());
		}
		return !ec;
	}

	std::vector<std::string> Directory::GetAllFiles(const std::string& dir) {
		std::vector<std::string> files;
		std::error_code ec;

		const std::filesystem::path base = std::filesystem::path(dir);
		if (!std::filesystem::exists(base, ec) || ec) return {};
		if (!std::filesystem::is_directory(base, ec) || ec) return {};

		for (std::filesystem::directory_iterator it(base, ec), end; it != end && !ec; it.increment(ec)) {
			const std::filesystem::directory_entry& entry = *it;
			std::error_code ec2;
			if (entry.is_regular_file(ec2) && !ec2) {
				if (AssetRegistry::IsMetaFilePath(entry.path().string())) {
					continue;
				}
				files.push_back(entry.path().string());
			}
		}

		std::sort(files.begin(), files.end());
		return files;
	}

	std::vector<DirectoryEntry> Directory::GetEntries(const std::string& dir) {
		std::vector<DirectoryEntry> entries;
		std::error_code ec;

		const std::filesystem::path base(dir);
		if (!std::filesystem::exists(base, ec) || ec) return {};
		if (!std::filesystem::is_directory(base, ec) || ec) return {};

		for (std::filesystem::directory_iterator it(base, ec), end; it != end && !ec; it.increment(ec)) {
			const std::filesystem::directory_entry& entry = *it;
			std::error_code ec2;

			DirectoryEntry e;
			e.Path = entry.path().string();
			e.Name = entry.path().filename().string();
			e.IsDirectory = entry.is_directory(ec2) && !ec2;
			if (!e.IsDirectory && AssetRegistry::IsMetaFilePath(e.Path)) {
				continue;
			}

			entries.push_back(std::move(e));
		}

		// Sort: directories first, then alphabetical within each group
		std::sort(entries.begin(), entries.end(), [](const DirectoryEntry& a, const DirectoryEntry& b) {
			if (a.IsDirectory != b.IsDirectory)
				return a.IsDirectory > b.IsDirectory;
			return a.Name < b.Name;
		});

		return entries;
	}
}
