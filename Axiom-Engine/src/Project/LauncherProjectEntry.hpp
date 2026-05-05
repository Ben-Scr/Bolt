#pragma once

#include <string>

namespace Axiom {

	// Public-field struct — engine convention is PascalCase for public fields without
	// the m_/s_/k_ prefix. JSON serialization keeps the lowercase wire keys for
	// backwards compatibility with already-saved launcher.json files.
	struct LauncherProjectEntry {
		std::string Name;
		std::string Path;
		std::string LastOpened;
	};

} // namespace Axiom
