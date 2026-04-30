#pragma once

#include "Scene/ComponentCategory.hpp"
#include "Scene/Entity.hpp"

#include <string>

namespace Bolt {

	struct ComponentInfo {
		std::string displayName;
		std::string serializedName;
		std::string subcategory;
		ComponentCategory category;

		ComponentInfo() = default;
		ComponentInfo(const std::string& displayName, ComponentCategory category)
			: displayName(displayName), category(category) {
		}
		ComponentInfo(const std::string& displayName, const std::string& subcategory, ComponentCategory category)
			: displayName(displayName), subcategory(subcategory), category(category) {
		}

		bool (*has)(Entity) = nullptr;
		void (*add)(Entity) = nullptr;
		void (*remove)(Entity) = nullptr;
		void (*copyTo)(Entity src, Entity dst) = nullptr;
		void (*drawInspector)(Entity) = nullptr;
	};

} // namespace Bolt
