#pragma once

#include "Packages/PackageSourceType.hpp"

#include <cstdint>
#include <string>

namespace Axiom {

	struct PackageInfo {
		std::string Id;
		std::string Version;
		std::string Description;
		std::string Authors;
		std::string SourceName;
		PackageSourceType SourceType = PackageSourceType::NuGet;
		int64_t TotalDownloads = 0;
		bool Verified = false;
		bool IsInstalled = false;
		std::string InstalledVersion;
	};

} // namespace Axiom
