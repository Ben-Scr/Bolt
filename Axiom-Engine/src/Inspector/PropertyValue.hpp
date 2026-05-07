#pragma once

#include "Core/Export.hpp"
#include "Inspector/PropertyType.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace Axiom {

	// Flat tagged union holding any value of a known PropertyType. Trivially
	// copyable for the numeric / vector cases; std::string lives outside the
	// union so the type stays a regular C++ object.
	//
	// Equality is canonical: two PropertyValues compare equal iff their type
	// matches and every byte of the payload matches. The drawer uses this to
	// detect mixed-state across selections.
	//
	// String round-trip (ToString / FromString) lets the existing C# script
	// pipeline keep its "value as JSON string" wire format. Native code that
	// holds the descriptor doesn't need to touch the string form.
	struct PropertyValue {
		PropertyType Type = PropertyType::None;

		bool BoolValue = false;
		int64_t IntValue = 0;        // Holds any signed/enum/component-id integer
		uint64_t UIntValue = 0;       // Holds any unsigned/asset/entity ID
		double FloatValue = 0.0;
		std::array<float, 4> FloatVec{ 0, 0, 0, 0 };
		std::array<int32_t, 4> IntVec{ 0, 0, 0, 0 };
		std::string StringValue;

		// Component refs serialise as "<entityId>:<typeName>". The integer
		// portion lives in UIntValue; the type name lives in StringValue.

		bool operator==(const PropertyValue& other) const noexcept {
			if (Type != other.Type) return false;
			switch (Type) {
			case PropertyType::None:
				return true;
			case PropertyType::Bool:
				return BoolValue == other.BoolValue;
			case PropertyType::Int8:
			case PropertyType::Int16:
			case PropertyType::Int32:
			case PropertyType::Int64:
			case PropertyType::Enum:
			case PropertyType::FlagEnum:
				return IntValue == other.IntValue;
			case PropertyType::UInt8:
			case PropertyType::UInt16:
			case PropertyType::UInt32:
			case PropertyType::UInt64:
			case PropertyType::TextureRef:
			case PropertyType::AudioRef:
			case PropertyType::AssetRef:
			case PropertyType::SceneRef:
			case PropertyType::PrefabRef:
			case PropertyType::FontRef:
				return UIntValue == other.UIntValue;
			case PropertyType::EntityRef:
				// EntityRef may also be a prefab reference (StringValue=="prefab")
				return UIntValue == other.UIntValue && StringValue == other.StringValue;
			case PropertyType::Float:
			case PropertyType::Double:
				return FloatValue == other.FloatValue;
			case PropertyType::String:
				return StringValue == other.StringValue;
			case PropertyType::Vec2:
				return FloatVec[0] == other.FloatVec[0]
					&& FloatVec[1] == other.FloatVec[1];
			case PropertyType::Vec3:
				return FloatVec[0] == other.FloatVec[0]
					&& FloatVec[1] == other.FloatVec[1]
					&& FloatVec[2] == other.FloatVec[2];
			case PropertyType::Vec4:
			case PropertyType::Color:
				return FloatVec == other.FloatVec;
			case PropertyType::IntVec2:
				return IntVec[0] == other.IntVec[0]
					&& IntVec[1] == other.IntVec[1];
			case PropertyType::IntVec3:
				return IntVec[0] == other.IntVec[0]
					&& IntVec[1] == other.IntVec[1]
					&& IntVec[2] == other.IntVec[2];
			case PropertyType::IntVec4:
				return IntVec == other.IntVec;
			case PropertyType::ComponentRef:
				return UIntValue == other.UIntValue
					&& StringValue == other.StringValue;
			}
			return false;
		}

		bool operator!=(const PropertyValue& other) const noexcept { return !(*this == other); }

		// Round-trip helpers. Defined in PropertyValue.cpp so we don't pull in
		// <charconv>/<cstdio> through this header. AXIOM_API exports them
		// across the engine DLL boundary so the editor can call them.
		AXIOM_API std::string ToString() const;
		AXIOM_API static PropertyValue FromString(PropertyType type, const std::string& text);
	};

} // namespace Axiom
