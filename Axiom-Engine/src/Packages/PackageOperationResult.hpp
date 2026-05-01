#pragma once

#include <string>

namespace Axiom {

	struct PackageOperationResult {
		bool Success = false;
		std::string Message;
	};

} // namespace Axiom
