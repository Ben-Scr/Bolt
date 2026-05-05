#pragma once

#include "Assets/AssetRegistry.hpp"
#include "Audio/AudioManager.hpp"
#include "Core/Log.hpp"
#include "Core/UUID.hpp"
#include "Graphics/TextureManager.hpp"
#include "Scene/EntityHandle.hpp"
#include "Serialization/Json.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>

namespace Axiom {
	class Scene;
	struct ScriptComponent;
}

namespace Axiom::SceneSerializerInternal {
	// Shared scene/prefab format constants. Previously duplicated as
	// anonymous-namespace constexprs in SceneSerializer.cpp and
	// SceneSerializerDeserialize.cpp, where they could silently drift.
	inline constexpr int k_SceneFormatVersion = 1;
	inline constexpr float k_MinScaleAxis = 0.0001f;
}

namespace Axiom::Detail {
	// Canonical SerializeEntity / override-diff helpers. Definitions live in
	// SceneSerializer.cpp; declared here so SceneSerializerDeserialize.cpp
	// can call them too without re-implementing a divergent copy. Anonymous
	// namespaces can't cross translation units, so we use a named namespace.
	Json::Value SerializeEntity(Scene& scene, EntityHandle entity);
	Json::Value SerializeScriptFields(const ScriptComponent& scriptComponent);

	bool JsonEquivalent(const Json::Value& a, const Json::Value& b);
	void BuildOverridePatch(
		const Json::Value& prefabValue,
		const Json::Value& instanceValue,
		const std::string& prefix,
		Json::Value& overrides);
}

namespace Axiom::SceneSerializerShared {
	using Json::Value;

	inline float GetFloatMember(const Value& object, std::string_view key, float fallback = 0.0f) {
		const Value* value = object.FindMember(key);
		return value ? static_cast<float>(value->AsDoubleOr(fallback)) : fallback;
	}

	inline int GetIntMember(const Value& object, std::string_view key, int fallback = 0) {
		const Value* value = object.FindMember(key);
		return value ? value->AsIntOr(fallback) : fallback;
	}

	inline uint64_t GetUInt64Member(const Value& object, std::string_view key, uint64_t fallback = 0) {
		const Value* value = object.FindMember(key);
		if (!value) {
			return fallback;
		}

		if (value->IsString()) {
			const std::string valueStr = value->AsStringOr();
			try {
				return static_cast<uint64_t>(std::stoull(valueStr));
			}
			catch (...) {
				// Logged at warn level so corrupt asset/scene IDs (overflow,
				// non-numeric, garbage) are visible during load instead of
				// silently collapsing to the fallback. Most production data
				// has well-formed numeric strings, so this only triggers on
				// genuinely malformed input.
				const std::string memberName(key);
				AIM_CORE_WARN_TAG(
					"Serialization",
					"Failed to parse uint64 member '{}' (value: '{}')",
					memberName,
					valueStr);
				return fallback;
			}
		}

		return value->AsUInt64Or(fallback);
	}

	inline bool GetBoolMember(const Value& object, std::string_view key, bool fallback = false) {
		const Value* value = object.FindMember(key);
		return value ? value->AsBoolOr(fallback) : fallback;
	}

	inline std::string GetStringMember(const Value& object, std::string_view key, const std::string& fallback = {}) {
		const Value* value = object.FindMember(key);
		return value ? value->AsStringOr(fallback) : fallback;
	}

	inline const Value* GetObjectMember(const Value& object, std::string_view key) {
		const Value* value = object.FindMember(key);
		return value && value->IsObject() ? value : nullptr;
	}

	inline const Value* GetArrayMember(const Value& object, std::string_view key) {
		const Value* value = object.FindMember(key);
		return value && value->IsArray() ? value : nullptr;
	}

	inline bool IsUnsignedIntegerString(const std::string& value) {
		if (value.empty()) {
			return false;
		}

		return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
			return std::isdigit(ch) != 0;
		});
	}

	inline std::string NormalizeScriptAssetValue(std::string_view fieldType, const std::string& value) {
		if ((fieldType != "texture" && fieldType != "audio") || value.empty() || IsUnsignedIntegerString(value)) {
			return value;
		}

		const uint64_t assetId = AssetRegistry::GetOrCreateAssetUUID(value);
		return assetId != 0 ? std::to_string(assetId) : value;
	}

	inline TextureHandle LoadTextureFromValue(
		const Value& object,
		std::string_view assetKey,
		std::string_view pathKey,
		Filter filter = Filter::Point,
		Wrap u = Wrap::Clamp,
		Wrap v = Wrap::Clamp,
		UUID* outAssetId = nullptr) {
		uint64_t assetId = GetUInt64Member(object, assetKey, 0);
		if (assetId != 0) {
			TextureHandle handle = TextureManager::LoadTextureByUUID(assetId, filter, u, v);
			if (handle.IsValid()) {
				if (outAssetId) {
					*outAssetId = UUID(assetId);
				}
				return handle;
			}

			if (outAssetId) {
				*outAssetId = UUID(assetId);
			}
		}

		const std::string path = GetStringMember(object, pathKey);
		if (path.empty()) {
			if (outAssetId && assetId == 0) {
				*outAssetId = UUID(0);
			}
			return TextureHandle::Invalid();
		}

		TextureHandle handle = TextureManager::LoadTexture(path, filter, u, v);
		if (handle.IsValid()) {
			assetId = TextureManager::GetTextureAssetUUID(handle);
			if (outAssetId) {
				*outAssetId = UUID(assetId);
			}
		}
		else if (outAssetId) {
			*outAssetId = UUID(0);
		}

		return handle;
	}

	inline AudioHandle LoadAudioFromValue(const Value& object, std::string_view assetKey, std::string_view pathKey, UUID* outAssetId = nullptr) {
		uint64_t assetId = GetUInt64Member(object, assetKey, 0);
		if (assetId != 0) {
			AudioHandle handle = AudioManager::LoadAudioByUUID(assetId);
			if (handle.IsValid()) {
				if (outAssetId) {
					*outAssetId = UUID(assetId);
				}
				return handle;
			}
		}

		const std::string path = GetStringMember(object, pathKey);
		if (path.empty()) {
			if (outAssetId) {
				*outAssetId = UUID(0);
			}
			return AudioHandle();
		}

		AudioHandle handle = AudioManager::LoadAudio(path);
		if (handle.IsValid()) {
			assetId = AudioManager::GetAudioAssetUUID(handle);
			if (outAssetId) {
				*outAssetId = UUID(assetId);
			}
		}
		else if (outAssetId) {
			*outAssetId = UUID(0);
		}

		return handle;
	}

	inline std::string ValueToFieldString(const Value& value) {
		if (value.IsString()) {
			return value.AsStringOr();
		}
		if (value.IsBool()) {
			return value.AsBoolOr(false) ? "true" : "false";
		}
		if (value.IsNumber()) {
			std::ostringstream stream;
			stream.precision(15);
			stream << value.AsDoubleOr(0.0);
			return stream.str();
		}
		if (value.IsNull()) {
			return {};
		}
		return Json::Stringify(value, false);
	}
}
