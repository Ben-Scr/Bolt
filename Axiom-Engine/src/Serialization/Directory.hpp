#pragma once
#include "Serialization/DirectoryEntry.hpp"

#include <string>
#include <vector>

namespace Axiom {
	class Directory {
	public:
		static void Create(const std::string& dir, bool recursive = true);
		static bool Exists(const std::string& dir);
		static bool Delete(const std::string& path);
		static bool Move(const std::string& from, const std::string& to);
		static bool Rename(const std::string& path, const std::string& newName);

		static std::vector<std::string> GetAllFiles(const std::string& dir);
		static std::vector<DirectoryEntry> GetEntries(const std::string& dir);
	};
}
