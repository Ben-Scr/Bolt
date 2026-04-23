#pragma once
#include "Scripting/ScriptInstance.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace Bolt {

	struct ScriptComponent {
		std::vector<ScriptInstance> Scripts;

		// Pending field values from deserialization, applied when instance binds.
		// Key: "ClassName.FieldName", Value: string representation.
		std::unordered_map<std::string, std::string> PendingFieldValues;

		ScriptComponent() = default;

		void AddScript(const std::string& className) {
			Scripts.emplace_back(className);
		}

		bool HasScript(const std::string& className) const {
			for (const auto& s : Scripts) {
				if (s.GetClassName() == className) return true;
			}
			return false;
		}
	};

} 
