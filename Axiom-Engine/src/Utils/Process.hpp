#pragma once

#include "Core/Export.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace Axiom::Process {

	struct AXIOM_API Result {
		int ExitCode = -1;
		std::string Output;

		bool Succeeded() const { return ExitCode == 0; }
	};

	AXIOM_API Result Run(const std::vector<std::string>& command,
		const std::filesystem::path& workingDirectory = {});

	AXIOM_API bool LaunchDetached(const std::vector<std::string>& command,
		const std::filesystem::path& workingDirectory = {});

} // namespace Axiom::Process
