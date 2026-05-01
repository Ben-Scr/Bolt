#pragma once

#include <string>

namespace Axiom {

	struct DirectoryEntry {
		std::string Path;
		std::string Name;
		bool IsDirectory = false;
	};

} // namespace Axiom
