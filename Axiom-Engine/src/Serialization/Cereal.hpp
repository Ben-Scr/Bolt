#pragma once

#include <filesystem>
#include <fstream>
#include <string>

#include <cereal/archives/json.hpp>

namespace Axiom {
	class Cereal {
	public:
		template <typename T>
		static bool SerializeToJsonFile(const std::filesystem::path& path, const T& value, std::string& outError) {
			try {
				if (path.empty()) {
					outError = "Cannot serialize to an empty path.";
					return false;
				}

				const std::filesystem::path parent = path.parent_path();
				if (!parent.empty()) {
					std::filesystem::create_directories(parent);
				}

				std::ofstream output(path, std::ios::out | std::ios::trunc);
				if (!output.is_open()) {
					outError = "Unable to open file for writing: " + path.string();
					return false;
				}

				cereal::JSONOutputArchive archive(output);
				archive(value);
				return true;
			}
			catch (const std::exception& e) {
				outError = e.what();
				return false;
			}
		}

		template <typename T>
		static bool DeserializeFromJsonFile(const std::filesystem::path& path, T& outValue, std::string& outError) {
			try {
				std::ifstream input(path);
				if (!input.is_open()) {
					outError = "Unable to open file: " + path.string();
					return false;
				}

				cereal::JSONInputArchive archive(input);
				archive(outValue);
				return true;
			}
			catch (const std::exception& e) {
				outError = e.what();
				return false;
			}
		}
	};
}