#include "pch.hpp"
#include "Inspector/PropertyValue.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace Axiom {

	namespace {
		std::string FormatFloat(double v) {
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%g", v);
			return buf;
		}

		std::vector<std::string> Split(const std::string& s, char sep) {
			std::vector<std::string> out;
			std::size_t start = 0;
			while (true) {
				const std::size_t at = s.find(sep, start);
				out.push_back(s.substr(start, at == std::string::npos ? std::string::npos : at - start));
				if (at == std::string::npos) break;
				start = at + 1;
			}
			return out;
		}

		float ToFloat(const std::string& s) {
			return static_cast<float>(std::atof(s.c_str()));
		}

		int ToInt(const std::string& s) {
			return std::atoi(s.c_str());
		}
	}

	std::string PropertyValue::ToString() const {
		switch (Type) {
		case PropertyType::None:
			return {};
		case PropertyType::Bool:
			return BoolValue ? "true" : "false";
		case PropertyType::Int8:
		case PropertyType::Int16:
		case PropertyType::Int32:
		case PropertyType::Int64:
		case PropertyType::Enum:
		case PropertyType::FlagEnum:
			return std::to_string(IntValue);
		case PropertyType::UInt8:
		case PropertyType::UInt16:
		case PropertyType::UInt32:
		case PropertyType::UInt64:
			return std::to_string(UIntValue);
		case PropertyType::EntityRef:
			// Entity refs can also hold prefab references (the C# Entity type
			// transparently handles both); StringValue == "prefab" tags this
			// case so ToString reconstructs the "prefab:<id>" format.
			if (StringValue == "prefab" && UIntValue != 0) return "prefab:" + std::to_string(UIntValue);
			return UIntValue != 0 ? std::to_string(UIntValue) : std::string();
		case PropertyType::Float:
		case PropertyType::Double:
			return FormatFloat(FloatValue);
		case PropertyType::String:
			return StringValue;
		case PropertyType::Vec2: {
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%g,%g", FloatVec[0], FloatVec[1]);
			return buf;
		}
		case PropertyType::Vec3: {
			char buf[96];
			std::snprintf(buf, sizeof(buf), "%g,%g,%g", FloatVec[0], FloatVec[1], FloatVec[2]);
			return buf;
		}
		case PropertyType::Vec4:
		case PropertyType::Color: {
			char buf[128];
			std::snprintf(buf, sizeof(buf), "%g,%g,%g,%g",
				FloatVec[0], FloatVec[1], FloatVec[2], FloatVec[3]);
			return buf;
		}
		case PropertyType::IntVec2: {
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%d,%d", IntVec[0], IntVec[1]);
			return buf;
		}
		case PropertyType::IntVec3: {
			char buf[96];
			std::snprintf(buf, sizeof(buf), "%d,%d,%d", IntVec[0], IntVec[1], IntVec[2]);
			return buf;
		}
		case PropertyType::IntVec4: {
			char buf[128];
			std::snprintf(buf, sizeof(buf), "%d,%d,%d,%d",
				IntVec[0], IntVec[1], IntVec[2], IntVec[3]);
			return buf;
		}
		case PropertyType::TextureRef:
		case PropertyType::AudioRef:
		case PropertyType::AssetRef:
		case PropertyType::SceneRef:
		case PropertyType::FontRef:
			return UIntValue != 0 ? std::to_string(UIntValue) : std::string();
		case PropertyType::PrefabRef:
			return UIntValue != 0 ? "prefab:" + std::to_string(UIntValue) : std::string();
		case PropertyType::ComponentRef:
			if (UIntValue == 0) return std::string();
			return std::to_string(UIntValue) + ":" + StringValue;
		}
		return {};
	}

	PropertyValue PropertyValue::FromString(PropertyType type, const std::string& text) {
		PropertyValue v;
		v.Type = type;

		switch (type) {
		case PropertyType::None:
			break;
		case PropertyType::Bool:
			v.BoolValue = (text == "true" || text == "True" || text == "1");
			break;
		case PropertyType::Int8:
		case PropertyType::Int16:
		case PropertyType::Int32:
		case PropertyType::Int64:
		case PropertyType::Enum:
		case PropertyType::FlagEnum:
			v.IntValue = std::strtoll(text.c_str(), nullptr, 10);
			break;
		case PropertyType::UInt8:
		case PropertyType::UInt16:
		case PropertyType::UInt32:
		case PropertyType::UInt64:
			v.UIntValue = std::strtoull(text.c_str(), nullptr, 10);
			break;
		case PropertyType::EntityRef: {
			static constexpr std::string_view prefabPrefix = "prefab:";
			if (text.rfind(prefabPrefix, 0) == 0) {
				v.StringValue = "prefab";
				v.UIntValue = std::strtoull(text.c_str() + prefabPrefix.size(), nullptr, 10);
			}
			else {
				v.StringValue.clear();
				v.UIntValue = text.empty() ? 0 : std::strtoull(text.c_str(), nullptr, 10);
			}
			break;
		}
		case PropertyType::Float:
		case PropertyType::Double:
			v.FloatValue = std::atof(text.c_str());
			break;
		case PropertyType::String:
			v.StringValue = text;
			break;
		case PropertyType::Vec2:
		case PropertyType::Vec3:
		case PropertyType::Vec4:
		case PropertyType::Color: {
			const auto parts = Split(text, ',');
			const std::size_t n = (type == PropertyType::Vec2) ? 2
				: (type == PropertyType::Vec3) ? 3
				: 4;
			for (std::size_t i = 0; i < n && i < parts.size(); ++i) {
				v.FloatVec[i] = ToFloat(parts[i]);
			}
			break;
		}
		case PropertyType::IntVec2:
		case PropertyType::IntVec3:
		case PropertyType::IntVec4: {
			const auto parts = Split(text, ',');
			const std::size_t n = (type == PropertyType::IntVec2) ? 2
				: (type == PropertyType::IntVec3) ? 3
				: 4;
			for (std::size_t i = 0; i < n && i < parts.size(); ++i) {
				v.IntVec[i] = ToInt(parts[i]);
			}
			break;
		}
		case PropertyType::TextureRef:
		case PropertyType::AudioRef:
		case PropertyType::AssetRef:
		case PropertyType::SceneRef:
		case PropertyType::FontRef:
			v.UIntValue = text.empty() ? 0 : std::strtoull(text.c_str(), nullptr, 10);
			break;
		case PropertyType::PrefabRef: {
			static constexpr std::string_view prefix = "prefab:";
			if (text.rfind(prefix, 0) == 0) {
				v.UIntValue = std::strtoull(text.c_str() + prefix.size(), nullptr, 10);
			}
			else if (!text.empty()) {
				v.UIntValue = std::strtoull(text.c_str(), nullptr, 10);
			}
			break;
		}
		case PropertyType::ComponentRef: {
			const std::size_t sep = text.find(':');
			if (sep == std::string::npos) {
				v.UIntValue = 0;
				v.StringValue.clear();
			}
			else {
				v.UIntValue = std::strtoull(text.substr(0, sep).c_str(), nullptr, 10);
				v.StringValue = text.substr(sep + 1);
			}
			break;
		}
		}
		return v;
	}

} // namespace Axiom
