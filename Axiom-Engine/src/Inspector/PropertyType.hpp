#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// =============================================================================
// Property type ruleset (canonical)
// -----------------------------------------------------------------------------
// Every inspectable field on a component (native or C#) maps to exactly one
// PropertyType. The drawer, serializer, and reference picker all dispatch on
// this single enum, so adding a new supported type is a one-site change here
// plus a switch arm in PropertyDrawer.cpp / PropertyValue.cpp.
//
// | PropertyType  | Native C++ type           | C# type                 | Widget                       |
// |---------------|---------------------------|-------------------------|------------------------------|
// | Bool          | bool                      | bool                    | Tri-state checkbox           |
// | Int8          | int8_t / signed char      | sbyte                   | DragInt clamped [-128,127]   |
// | Int16         | int16_t / short           | short                   | DragInt clamped              |
// | Int32         | int32_t / int             | int                     | DragInt                      |
// | Int64         | int64_t / long long       | long                    | InputScalar (S64)            |
// | UInt8         | uint8_t / unsigned char   | byte                    | DragInt clamped [0,255]      |
// | UInt16        | uint16_t / unsigned short | ushort                  | DragInt clamped              |
// | UInt32        | uint32_t / unsigned int   | uint                    | DragInt non-negative         |
// | UInt64        | uint64_t                  | ulong                   | InputScalar (U64)            |
// | Float         | float                     | float                   | DragFloat                    |
// | Double        | double                    | double                  | DragScalar (Double)          |
// | String        | std::string               | string                  | InputText                    |
// | Vec2          | Vec2 / glm::vec2          | Vector2                 | 2-channel DragFloat          |
// | Vec3          | glm::vec3                 | Vector3                 | 3-channel DragFloat          |
// | Vec4          | Vec4 / glm::vec4          | Vector4                 | 4-channel DragFloat          |
// | IntVec2       | glm::ivec2                | Vector2Int              | 2-channel DragInt            |
// | IntVec3       | glm::ivec3                | Vector3Int              | 3-channel DragInt            |
// | IntVec4       | glm::ivec4                | Vector4Int              | 4-channel DragInt            |
// | Color         | Color                     | Color                   | ColorEdit4 (per-chan fallback)|
// | Enum          | enum class                | enum                    | Combo (single value)         |
// | FlagEnum      | enum class (bitflags)     | [Flags] enum            | Multi-checkbox combo         |
// | TextureRef    | TextureHandle             | Texture / TextureRef    | Reference picker (Texture)   |
// | AudioRef      | AudioHandle               | Audio / AudioRef        | Reference picker (Audio)     |
// | AssetRef      | AssetGUID                 | (any)                   | Reference picker (any kind)  |
// | SceneRef      | AssetGUID                 | (Scene asset)           | Reference picker (Scene)     |
// | EntityRef     | EntityRuntimeID (uint64)* | Entity                  | Reference picker (Entity)    |
//   * EntityRef is polymorphic: the value may be a live entity ID OR a
//     prefab asset reference. PropertyValue stores the integer in UIntValue
//     and tags prefab references with `StringValue == "prefab"`. ToString
//     reconstructs the `"prefab:<id>"` wire format when the tag is set;
//     equality compares both fields. Drawer + picker handle both cases via
//     the same EntityRef code path. Code reading PropertyValue.UIntValue
//     directly MUST also check StringValue to distinguish the two.
// | PrefabRef     | AssetGUID                 | (Prefab asset)          | Reference picker (Prefab)    |
// | ComponentRef  | "<entityId>:<typeName>"   | Component subclass      | Reference picker (Component) |
//
// Multi-selection rules
// ---------------------
//  * Scalars (Bool, integers, Float, Double, String, Enum):
//      - All entities equal       → widget shows the value normally.
//      - Mismatched               → widget shows "—" (or ImGui mixed flag for
//                                    Bool / FlagEnum).
//      - Editing writes the new value to ALL selected entities.
//  * Vectors / Color (Vec*, IntVec*, Color):
//      - Mixed state is per-channel. Editing only the changed channel writes
//        to all entities; untouched channels are left alone per-entity.
//  * References (Texture/Audio/Asset/Scene/Entity/Prefab/Component):
//      - All entities equal       → button shows the asset/entity name + path.
//      - Mismatched               → button shows "—" with a tooltip that lists
//                                    "Multiple values".
//      - Picker / drag-drop / clear writes to ALL selected entities.
//  * FlagEnum:
//      - Each flag bit is sampled independently across the selection. A bit
//        that is uniformly on/off shows checked/unchecked; a bit that differs
//        across entities renders with the ImGui mixed-value flag.
//
// Reference behaviour
// -------------------
// All reference types share the same picker UX:
//   1. A button labelled with the current target name, or "(None)", or "—"
//      when mixed across selection. Tooltip shows the asset path / scene name.
//   2. Click → modal picker with search field and a flat list. Picking writes
//      the new reference to every selected entity. The picker also offers a
//      "(None)" entry to clear.
//   3. Drag-drop accepts the appropriate payload type (ASSET_BROWSER_ITEM for
//      assets, HIERARCHY_ENTITY for entities, COMPONENT_REF for component
//      references). Drops are validated against the expected asset kind /
//      component type before being accepted.
//   4. Right-click context menu has "Clear" which writes a null reference.
//
// Prefab override hooks
// ---------------------
// PropertyDescriptor exposes a stable name. When prefab override tracking
// lands, the diff is a simple per-name comparison between the prefab's source
// PropertyValue and the instance's PropertyValue, so no per-type override code
// is needed: the variant equality is the diff.
// =============================================================================

namespace Axiom {

	enum class PropertyType : uint8_t {
		None = 0,

		// Booleans
		Bool,

		// Signed integers
		Int8,
		Int16,
		Int32,
		Int64,

		// Unsigned integers
		UInt8,
		UInt16,
		UInt32,
		UInt64,

		// Floating point
		Float,
		Double,

		// Strings
		String,

		// Vectors
		Vec2,
		Vec3,
		Vec4,
		IntVec2,
		IntVec3,
		IntVec4,

		// Colour
		Color,

		// Enumerations
		Enum,
		FlagEnum,

		// References
		TextureRef,
		AudioRef,
		FontRef,
		AssetRef,     // Generic asset of unspecified kind (filtered by metadata)
		SceneRef,
		EntityRef,
		PrefabRef,
		ComponentRef,
	};

	struct EnumOption {
		std::string Name;
		int64_t Value = 0;
	};

	// Shared metadata for an Enum / FlagEnum descriptor. Lives outside
	// PropertyMetadata so language-agnostic code can build it from either
	// magic_enum (C++) or System.Reflection (C#).
	struct EnumDescriptor {
		std::vector<EnumOption> Options;
		bool IsFlags = false;
	};

	constexpr std::string_view ToString(PropertyType type) {
		switch (type) {
		case PropertyType::None:         return "none";
		case PropertyType::Bool:         return "bool";
		case PropertyType::Int8:         return "sbyte";
		case PropertyType::Int16:        return "short";
		case PropertyType::Int32:        return "int";
		case PropertyType::Int64:        return "long";
		case PropertyType::UInt8:        return "byte";
		case PropertyType::UInt16:       return "ushort";
		case PropertyType::UInt32:       return "uint";
		case PropertyType::UInt64:       return "ulong";
		case PropertyType::Float:        return "float";
		case PropertyType::Double:       return "double";
		case PropertyType::String:       return "string";
		case PropertyType::Vec2:         return "vector2";
		case PropertyType::Vec3:         return "vector3";
		case PropertyType::Vec4:         return "vector4";
		case PropertyType::IntVec2:      return "vector2Int";
		case PropertyType::IntVec3:      return "vector3Int";
		case PropertyType::IntVec4:      return "vector4Int";
		case PropertyType::Color:        return "color";
		case PropertyType::Enum:         return "enum";
		case PropertyType::FlagEnum:     return "flagenum";
		case PropertyType::TextureRef:   return "texture";
		case PropertyType::AudioRef:     return "audio";
		case PropertyType::FontRef:      return "font";
		case PropertyType::AssetRef:     return "asset";
		case PropertyType::SceneRef:     return "scene";
		case PropertyType::EntityRef:    return "entity";
		case PropertyType::PrefabRef:    return "prefab";
		case PropertyType::ComponentRef: return "component";
		}
		return "none";
	}

	// Inverse of ToString. Used to translate the C# JSON `type` field back into
	// the unified PropertyType enum. Component refs carry a ":TypeName" suffix
	// in the JSON which the caller handles separately, so this matches the
	// bare "component" tag.
	constexpr PropertyType PropertyTypeFromString(std::string_view text) {
		if (text == "bool")        return PropertyType::Bool;
		if (text == "sbyte")       return PropertyType::Int8;
		if (text == "short")       return PropertyType::Int16;
		if (text == "int")         return PropertyType::Int32;
		if (text == "long")        return PropertyType::Int64;
		if (text == "byte")        return PropertyType::UInt8;
		if (text == "ushort")      return PropertyType::UInt16;
		if (text == "uint")        return PropertyType::UInt32;
		if (text == "ulong")       return PropertyType::UInt64;
		if (text == "float")       return PropertyType::Float;
		if (text == "double")      return PropertyType::Double;
		if (text == "string")      return PropertyType::String;
		if (text == "vector2")     return PropertyType::Vec2;
		if (text == "vector3")     return PropertyType::Vec3;
		if (text == "vector4")     return PropertyType::Vec4;
		if (text == "vector2Int")  return PropertyType::IntVec2;
		if (text == "vector3Int")  return PropertyType::IntVec3;
		if (text == "vector4Int")  return PropertyType::IntVec4;
		if (text == "color")       return PropertyType::Color;
		if (text == "enum")        return PropertyType::Enum;
		if (text == "flagenum")    return PropertyType::FlagEnum;
		if (text == "texture")     return PropertyType::TextureRef;
		if (text == "audio")       return PropertyType::AudioRef;
		if (text == "asset")       return PropertyType::AssetRef;
		if (text == "scene")       return PropertyType::SceneRef;
		if (text == "font")        return PropertyType::FontRef;
		if (text == "entity")      return PropertyType::EntityRef;
		if (text == "prefab")      return PropertyType::PrefabRef;
		if (text == "component")   return PropertyType::ComponentRef;
		return PropertyType::None;
	}

} // namespace Axiom
