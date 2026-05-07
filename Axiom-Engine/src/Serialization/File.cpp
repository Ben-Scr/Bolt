#include "pch.hpp"
#include "File.hpp"

namespace Axiom {
	bool File::Exists(const std::string& path) {
		return std::filesystem::exists(path);
	}

	bool File::WriteAllText(const std::string& path, const std::string& text) {
		// Stage to a sibling .tmp first, then rename. A crash mid-stream then loses only
		// the staging file, leaving the original intact. The previous in-place truncate-
		// then-write produced corrupted scenes / project files on power loss or process
		// kill. The rename is atomic on NTFS within a volume and on POSIX always.
		const std::filesystem::path target(path);
		std::filesystem::path tmp = target;
		tmp += ".tmp";

		{
			std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
			if (!file.is_open()) {
				AIM_CORE_ERROR("File couldn't be opened for writing: {}", tmp.string());
				return false;
			}

			file.write(text.c_str(), text.size());
			if (!file.good()) {
				AIM_CORE_ERROR("Write failed (disk full / IO error): {}", tmp.string());
				file.close();
				std::error_code ec;
				std::filesystem::remove(tmp, ec);
				return false;
			}
			file.close();
			if (file.fail()) {
				AIM_CORE_ERROR("Close failed for: {}", tmp.string());
				std::error_code ec;
				std::filesystem::remove(tmp, ec);
				return false;
			}
		}

		std::error_code ec;
		std::filesystem::rename(tmp, target, ec);
		if (ec) {
			// On Windows, rename fails if the target exists with certain attributes; fall
			// back to remove+rename. POSIX rename is unconditional, so this branch only
			// triggers on Windows-specific failures.
			std::filesystem::remove(target, ec);
			ec.clear();
			std::filesystem::rename(tmp, target, ec);
			if (ec) {
				AIM_CORE_ERROR("Rename failed: {} -> {} ({})", tmp.string(), target.string(), ec.message());
				std::error_code cleanup;
				std::filesystem::remove(tmp, cleanup);
				return false;
			}
		}

		return true;
	}

	std::string File::ReadAllText(const std::string& path) {
		std::ifstream file(path, std::ios::binary);
		if (!file.is_open()) {
			AIM_CORE_ERROR("File couldn't be opened for reading: {}", path);
			return {};
		}

		return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	}

	std::vector<std::uint8_t> File::ReadAllBytes(const std::string& path) {
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file.is_open()) {
			AIM_CORE_ERROR("File couldn't be opened for reading: {}", path);
			return {};
		}

		std::streamsize size = file.tellg();

		if (size < 0) {
			AIM_CORE_ERROR("tellg() failed while reading file: {}", path);
			return {};
		}

		file.seekg(0, std::ios::beg);

		std::vector<std::uint8_t> buffer(static_cast<std::size_t>(size));

		if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
			AIM_CORE_ERROR("File reading failed: {}", path);
			return {};
		}

		file.close();
		return buffer;
	}
}
