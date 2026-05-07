#pragma once
#include "Core/Export.hpp"
#include "Core/Log.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Axiom {
	class AXIOM_API File {
	public:
		File() = delete;

		static bool Exists(const std::string& path);

		// Writes `text` to `path` atomically: stages to `path + ".tmp"` then renames over
		// the destination. Returns true on success. Returns false (without modifying the
		// original file) if the staging open fails, the write fails, the rename fails, or
		// the target volume is full. Callers MUST check the return value before treating
		// data as persisted (e.g. before clearing a dirty flag).
		[[nodiscard]] static bool WriteAllText(const std::string& path, const std::string& text);

		template<size_t Size>
		static void WriteAllLines(const std::string& path, const std::array<std::string, Size> lines) {
			std::ofstream file(path);
			if (!file.is_open()) {
				AIM_CORE_ERROR("File couldn't be opened for writing: {}", path);
				return;
			}

			for (const std::string& line : lines) {
				file.write(line.c_str(), line.size());
				file.write("\n", 1);
			}

			file.close();
		}

		template<size_t Size>
		static void WriteAllBytes(const std::string& path, const std::array<std::uint8_t, Size> bytes) {
			std::ofstream file(path, std::ios::binary);
			if (!file.is_open()) {
				AIM_CORE_ERROR("File couldn't be opened for writing: {}", path);
				return;
			}

			file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
			file.close();
		}

		template<typename T>
		static void WriteObject(const std::string& path, const T& obj) {
			std::ofstream file(path, std::ios::out | std::ios::binary);
			if (!file.is_open()) {
				AIM_CORE_ERROR("File couldn't be opened for writing: {}", path);
				return;
			}

			file.write(reinterpret_cast<const char*>(&obj), sizeof(T));
		}

		template<typename T>
		static T ReadObject(const std::string& path) {
			std::ifstream file(path, std::ios::binary);
			if (!file.is_open()) {
				AIM_CORE_ERROR("File couldn't be opened for reading: {}", path);
				return T{};
			}

			T obj{};
			file.read(reinterpret_cast<char*>(&obj), sizeof(T));
			return obj;
		}

		static std::string ReadAllText(const std::string& path);
		static std::vector<std::uint8_t> ReadAllBytes(const std::string& path);
	};
}