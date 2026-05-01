#pragma once

#include "Editor/ExternalEditorType.hpp"

#include <string>

namespace Axiom {

	struct ExternalEditorInfo {
		ExternalEditorType Type = ExternalEditorType::Auto;
		std::string DisplayName;
		std::string ExecutablePath;
		bool Available = false;
	};

} // namespace Axiom
