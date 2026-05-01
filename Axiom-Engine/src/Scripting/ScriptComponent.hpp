#pragma once
#include "Scripting/ScriptInstance.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace Axiom {

	struct ScriptComponent {
		std::vector<ScriptInstance> Scripts;
		std::vector<std::string> ManagedComponents;

		// Pending field values from deserialization, applied when instance binds.
		// Key: "ClassName.FieldName", Value: string representation.
		std::unordered_map<std::string, std::string> PendingFieldValues;

		ScriptComponent() = default;

		void AddScript(const std::string& className, ScriptType type = ScriptType::Managed) {
			if (className.empty() || HasScript(className, type)) {
				return;
			}

			Scripts.emplace_back(className, type);
		}

		bool HasScript(const std::string& className) const {
			for (const auto& s : Scripts) {
				if (s.GetClassName() == className) return true;
			}
			return false;
		}

		bool HasScript(const std::string& className, ScriptType type) const {
			for (const auto& s : Scripts) {
				if (s.GetClassName() == className && s.GetType() == type) return true;
			}
			return false;
		}

		void AddManagedComponent(const std::string& className) {
			if (className.empty() || HasManagedComponent(className)) {
				return;
			}

			ManagedComponents.push_back(className);
		}

		bool HasManagedComponent(const std::string& className) const {
			for (const auto& componentClass : ManagedComponents) {
				if (componentClass == className) return true;
			}
			return false;
		}

		bool RemoveManagedComponent(const std::string& className) {
			for (auto it = ManagedComponents.begin(); it != ManagedComponents.end(); ++it) {
				if (*it == className) {
					ManagedComponents.erase(it);
					const std::string prefix = className + ".";
					for (auto fieldIt = PendingFieldValues.begin(); fieldIt != PendingFieldValues.end(); ) {
						if (fieldIt->first.rfind(prefix, 0) == 0) {
							fieldIt = PendingFieldValues.erase(fieldIt);
						}
						else {
							++fieldIt;
						}
					}
					return true;
				}
			}
			return false;
		}
	};

} 
