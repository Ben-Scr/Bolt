#include "pch.hpp"
#include "File.hpp"

namespace Axiom {
	bool File::Exists(const std::string& path) {
		return std::filesystem::exists(path);
	}

	void File::WriteAllText(const std::string& path, const std::string& text) {
		std::ofstream file(path, std::ios::binary);
		if (!file.is_open()) {
			AIM_CORE_ERROR("File couldn't be opened for writing: {}", path);
			return;
		}

		file.write(text.c_str(), text.size());
		file.close();
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
