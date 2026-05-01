#pragma once
#include <string>

namespace Axiom {
	struct NameComponent {
		std::string Name{ "Entity ()"};
		NameComponent() = default;
		explicit NameComponent(std::string name) : Name(std::move(name)) {}
	};
}