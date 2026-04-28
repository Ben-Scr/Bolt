#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Bolt {

	enum class ScriptType : uint8_t { Unknown, Managed, Native };

	inline constexpr const char* ScriptTypeToString(ScriptType type)
	{
		switch (type) {
			case ScriptType::Managed: return "Managed";
			case ScriptType::Native:  return "Native";
			default:                  return "Unknown";
		}
	}

	inline bool ScriptTypeEqualsIgnoreCase(std::string_view a, std::string_view b)
	{
		if (a.size() != b.size()) {
			return false;
		}

		for (std::size_t i = 0; i < a.size(); ++i) {
			char ca = a[i];
			char cb = b[i];
			if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
			if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
			if (ca != cb) {
				return false;
			}
		}

		return true;
	}

	inline ScriptType ScriptTypeFromString(std::string_view value)
	{
		if (ScriptTypeEqualsIgnoreCase(value, "Managed") || ScriptTypeEqualsIgnoreCase(value, "CSharp") || value == "C#") {
			return ScriptType::Managed;
		}
		if (ScriptTypeEqualsIgnoreCase(value, "Native") || ScriptTypeEqualsIgnoreCase(value, "Cpp") || value == "C++") {
			return ScriptType::Native;
		}

		return ScriptType::Unknown;
	}

} // namespace Bolt
