#include "pch.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/SceneSerializerShared.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Scene/Scene.hpp"
#include "Scene/EntityHelper.hpp"
#include "Components/General/NameComponent.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/PrefabInstanceComponent.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/General/UUIDComponent.hpp"
#include "Scene/Entity.hpp"
#include "Components/Tags.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Components/Physics/FastBody2DComponent.hpp"
#include "Components/Physics/FastBoxCollider2DComponent.hpp"
#include "Components/Physics/FastCircleCollider2DComponent.hpp"
#include "Graphics/TextureManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Physics/PhysicsTypes.hpp"
#include "Core/Application.hpp"
#include "Core/Log.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/ComponentInfo.hpp"

#include <cmath>
#include <cctype>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Axiom {

	using Json::Value;
	using namespace SceneSerializerShared;

	namespace {
		static constexpr int SCENE_FORMAT_VERSION = 1;
		static constexpr float k_MinScaleAxis = 0.0001f;

		Value SerializeScriptFields(const ScriptComponent& scriptComponent) {
			Value fieldsByClass = Value::MakeObject();
			auto& callbacks = ScriptEngine::GetCallbacks();

			for (const ScriptInstance& instance : scriptComponent.Scripts) {
				if (instance.GetType() == ScriptType::Native) {
					continue;
				}

				const char* rawJson = nullptr;
				const bool hasLiveInstance = instance.HasManagedInstance();

				if (hasLiveInstance && callbacks.GetScriptFields) {
					rawJson = callbacks.GetScriptFields(static_cast<int32_t>(instance.GetGCHandle()));
				}
				else if (callbacks.GetClassFieldDefs) {
					rawJson = callbacks.GetClassFieldDefs(instance.GetClassName().c_str());
				}

				if (!rawJson || !*rawJson) {
					continue;
				}

				Value fieldArray;
				std::string parseError;
				if (!Json::TryParse(rawJson, fieldArray, &parseError) || !fieldArray.IsArray()) {
					AIM_CORE_WARN_TAG(
						"SceneSerializer",
						"Failed to parse script field metadata for {}: {}",
						instance.GetClassName(),
						parseError);
					continue;
				}

				if (!hasLiveInstance && !scriptComponent.PendingFieldValues.empty()) {
					const std::string prefix = instance.GetClassName() + ".";
					for (Value& fieldValue : fieldArray.GetArray()) {
						if (!fieldValue.IsObject()) {
							continue;
						}

						const std::string fieldName = GetStringMember(fieldValue, "name");
						if (fieldName.empty()) {
							continue;
						}

						const auto pendingValueIt = scriptComponent.PendingFieldValues.find(prefix + fieldName);
						if (pendingValueIt == scriptComponent.PendingFieldValues.end()) {
							continue;
						}

						fieldValue.AddMember("value", Value(pendingValueIt->second));
					}
				}

				for (Value& fieldValue : fieldArray.GetArray()) {
					if (!fieldValue.IsObject()) {
						continue;
					}

					const std::string fieldType = GetStringMember(fieldValue, "type");
					const std::string currentValue = GetStringMember(fieldValue, "value");
					const std::string normalizedValue = NormalizeScriptAssetValue(fieldType, currentValue);
					if (normalizedValue != currentValue) {
						fieldValue.AddMember("value", Value(normalizedValue));
					}
				}

				if (!fieldArray.GetArray().empty()) {
					fieldsByClass.AddMember(instance.GetClassName(), std::move(fieldArray));
				}
			}

			for (const std::string& className : scriptComponent.ManagedComponents) {
				if (className.empty() || !callbacks.GetClassFieldDefs) {
					continue;
				}

				const char* rawJson = callbacks.GetClassFieldDefs(className.c_str());
				if (!rawJson || !*rawJson) {
					continue;
				}

				Value fieldArray;
				std::string parseError;
				if (!Json::TryParse(rawJson, fieldArray, &parseError) || !fieldArray.IsArray()) {
					AIM_CORE_WARN_TAG(
						"SceneSerializer",
						"Failed to parse managed component field metadata for {}: {}",
						className,
						parseError);
					continue;
				}

				const std::string prefix = className + ".";
				for (Value& fieldValue : fieldArray.GetArray()) {
					if (!fieldValue.IsObject()) {
						continue;
					}

					const std::string fieldName = GetStringMember(fieldValue, "name");
					if (fieldName.empty()) {
						continue;
					}

					const auto pendingValueIt = scriptComponent.PendingFieldValues.find(prefix + fieldName);
					if (pendingValueIt != scriptComponent.PendingFieldValues.end()) {
						fieldValue.AddMember("value", Value(pendingValueIt->second));
					}

					const std::string fieldType = GetStringMember(fieldValue, "type");
					const std::string currentValue = GetStringMember(fieldValue, "value");
					const std::string normalizedValue = NormalizeScriptAssetValue(fieldType, currentValue);
					if (normalizedValue != currentValue) {
						fieldValue.AddMember("value", Value(normalizedValue));
					}
				}

				if (!fieldArray.GetArray().empty()) {
					fieldsByClass.AddMember(className, std::move(fieldArray));
				}
			}

			return fieldsByClass;
		}

		// NOTE: A divergent copy of SerializeEntity used to live here; it omitted
		// parentUuid (and emitted legacy keys the canonical writer dropped),
		// causing prefab override-diff to flag spurious deletions on every save.
		// All callers in this TU now use Detail::SerializeEntity (defined in
		// SceneSerializer.cpp) so the diff sees the same JSON shape the writer wrote.

		EntityOrigin EntityOriginFromString(const std::string& value) {
			if (value == "Scene" || value == "scene") {
				return EntityOrigin::Scene;
			}
			if (value == "Prefab" || value == "prefab") {
				return EntityOrigin::Prefab;
			}
			return EntityOrigin::Runtime;
		}

		void RemoveObjectMember(Value& value, std::string_view key) {
			if (!value.IsObject()) {
				return;
			}

			auto& members = value.GetObject();
			members.erase(
				std::remove_if(members.begin(), members.end(), [key](const auto& member) {
					return member.first == key;
				}),
				members.end());
		}

		void RemoveEntityIdentityMembers(Value& value) {
			RemoveObjectMember(value, "Origin");
			RemoveObjectMember(value, "RuntimeID");
			RemoveObjectMember(value, "EntityID");
			RemoveObjectMember(value, "SceneGUID");
			RemoveObjectMember(value, "PrefabGUID");
			RemoveObjectMember(value, "uuid");
		}

		void RemovePrefabRuntimeIdentityMembers(Value& value) {
			RemoveObjectMember(value, "Origin");
			RemoveObjectMember(value, "RuntimeID");
			RemoveObjectMember(value, "EntityID");
			RemoveObjectMember(value, "SceneGUID");
			RemoveObjectMember(value, "PrefabGUID");
		}

		void RemoveEntityComparisonIdentityMembers(Value& value) {
			RemoveEntityIdentityMembers(value);
			RemoveObjectMember(value, "parentUuid");
		}

		bool JsonEquivalent(const Value& left, const Value& right) {
			// Structural equality via Json::operator==, matching the canonical impl in
			// SceneSerializer.cpp. Per-leaf Stringify+compare was a real hot-path cost
			// (called O(N) per ComputeInstanceOverrides, which the inspector calls per
			// frame on every prefab instance).
			return left == right;
		}

		// Recursive diff; duplicated from SceneSerializer.cpp to keep ApplyOverrides internals private.
		void BuildOverridePatch(const Value& prefabValue, const Value& instanceValue,
			const std::string& prefix, Value& overrides) {
			if (prefabValue.IsObject() && instanceValue.IsObject()) {
				for (const auto& [key, instanceMember] : instanceValue.GetObject()) {
					const Value* prefabMember = prefabValue.FindMember(key);
					const std::string path = prefix.empty() ? key : prefix + "." + key;
					if (prefabMember && prefabMember->IsObject() && instanceMember.IsObject()) {
						BuildOverridePatch(*prefabMember, instanceMember, path, overrides);
						continue;
					}

					if (!prefabMember || !JsonEquivalent(*prefabMember, instanceMember)) {
						overrides.AddMember(path, instanceMember);
					}
				}
				return;
			}

			if (!JsonEquivalent(prefabValue, instanceValue)) {
				overrides.AddMember(prefix, instanceValue);
			}
		}

		std::vector<std::string> SplitOverridePath(const std::string& path) {
			std::vector<std::string> parts;
			size_t start = 0;
			while (start <= path.size()) {
				const size_t dot = path.find('.', start);
				parts.push_back(path.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
				if (dot == std::string::npos) {
					break;
				}
				start = dot + 1;
			}
			return parts;
		}

		bool ApplyScriptFieldOverride(Value& entityValue, const std::vector<std::string>& parts, const Value& overrideValue) {
			if (parts.size() != 2) {
				return false;
			}

			Value* scriptFields = entityValue.FindMember("ScriptFields");
			if (!scriptFields || !scriptFields->IsObject()) {
				return false;
			}

			Value* classFields = scriptFields->FindMember(parts[0]);
			if (!classFields || !classFields->IsArray()) {
				return false;
			}

			for (Value& fieldValue : classFields->GetArray()) {
				if (!fieldValue.IsObject() || GetStringMember(fieldValue, "name") != parts[1]) {
					continue;
				}

				fieldValue.AddMember("value", overrideValue);
				return true;
			}

			Value fieldValue = Value::MakeObject();
			fieldValue.AddMember("name", Value(parts[1]));
			fieldValue.AddMember("value", overrideValue);
			classFields->Append(std::move(fieldValue));
			return true;
		}

		bool ApplyOverridePath(Value& entityValue, const std::string& path, const Value& overrideValue) {
			if (path.empty()) {
				return false;
			}

			const std::vector<std::string> parts = SplitOverridePath(path);
			if (parts.empty()) {
				return false;
			}

			if (ApplyScriptFieldOverride(entityValue, parts, overrideValue)) {
				return true;
			}

			Value* current = &entityValue;
			for (size_t i = 0; i + 1 < parts.size(); ++i) {
				if (!current->IsObject()) {
					return false;
				}

				Value* child = current->FindMember(parts[i]);
				if (!child) {
					child = &current->AddMember(parts[i], Value::MakeObject());
				}
				current = child;
			}

			current->AddMember(parts.back(), overrideValue);
			return true;
		}

		const Value* GetOverrideValueAtPath(const Value& entityValue, const std::string& path) {
			const std::vector<std::string> parts = SplitOverridePath(path);
			if (parts.empty()) {
				return nullptr;
			}

			if (parts.size() == 2) {
				if (const Value* scriptFields = GetObjectMember(entityValue, "ScriptFields")) {
					if (const Value* classFields = scriptFields->FindMember(parts[0]); classFields && classFields->IsArray()) {
						for (const Value& fieldValue : classFields->GetArray()) {
							if (!fieldValue.IsObject() || GetStringMember(fieldValue, "name") != parts[1]) {
								continue;
							}

							return fieldValue.FindMember("value");
						}
					}
				}
			}

			const Value* current = &entityValue;
			for (const std::string& part : parts) {
				if (!current->IsObject()) {
					return nullptr;
				}

				current = current->FindMember(part);
				if (!current) {
					return nullptr;
				}
			}
			return current;
		}

		void ApplyOverrides(Value& prefabEntityValue, const Value& instanceValue);

		struct PrefabDefinition {
			std::vector<Value> Entities;
		};

		uint64_t GetPrefabSourceId(const Value& entityValue) {
			return GetUInt64Member(entityValue, "uuid", 0);
		}

		bool ReadPrefabDefinitionFromRoot(const Value& root, PrefabDefinition& outDefinition) {
			outDefinition.Entities.clear();
			if (!root.IsObject()) {
				return false;
			}

			if (const Value* entities = GetArrayMember(root, "Entities")) {
				for (const Value& entityValue : entities->GetArray()) {
					if (entityValue.IsObject()) {
						outDefinition.Entities.push_back(entityValue);
					}
				}
				if (!outDefinition.Entities.empty()) {
					return true;
				}
			}

			if (const Value* entityValue = GetObjectMember(root, "Entity")) {
				outDefinition.Entities.push_back(*entityValue);
				return true;
			}
			if (const Value* entityValue = GetObjectMember(root, "prefab")) {
				outDefinition.Entities.push_back(*entityValue);
				return true;
			}

			if (root.FindMember("Transform2D") || root.FindMember("Scripts") || root.FindMember("Name")) {
				outDefinition.Entities.push_back(root);
				return true;
			}

			return false;
		}

		bool LoadPrefabDefinition(uint64_t prefabGuid, PrefabDefinition& outDefinition);

		void ApplyEntityOverrides(PrefabDefinition& definition, const Value& instanceValue) {
			if (definition.Entities.empty()) {
				return;
			}

			// Backwards-compatible root override payload.
			ApplyOverrides(definition.Entities.front(), instanceValue);

			const Value* entityOverrides = GetObjectMember(instanceValue, "EntityOverrides");
			if (!entityOverrides) {
				return;
			}

			std::unordered_map<uint64_t, Value*> entitiesBySourceId;
			entitiesBySourceId.reserve(definition.Entities.size());
			for (Value& entityValue : definition.Entities) {
				const uint64_t sourceId = GetPrefabSourceId(entityValue);
				if (sourceId != 0) {
					entitiesBySourceId[sourceId] = &entityValue;
				}
			}

			for (const auto& [sourceIdText, overrides] : entityOverrides->GetObject()) {
				uint64_t sourceId = 0;
				try {
					sourceId = static_cast<uint64_t>(std::stoull(sourceIdText));
				}
				catch (...) {
					continue;
				}

				auto targetIt = entitiesBySourceId.find(sourceId);
				if (targetIt == entitiesBySourceId.end() || !overrides.IsObject()) {
					continue;
				}

				Value wrapper = Value::MakeObject();
				wrapper.AddMember("Overrides", overrides);
				ApplyOverrides(*targetIt->second, wrapper);
			}
		}

		std::unordered_map<uint64_t, const Value*> BuildSourceEntityMap(const PrefabDefinition& definition) {
			std::unordered_map<uint64_t, const Value*> bySourceId;
			bySourceId.reserve(definition.Entities.size());
			for (const Value& entityValue : definition.Entities) {
				const uint64_t sourceId = GetPrefabSourceId(entityValue);
				if (sourceId != 0) {
					bySourceId[sourceId] = &entityValue;
				}
			}
			return bySourceId;
		}

		uint64_t GetPrefabInstanceSourceId(Scene& scene, EntityHandle entity) {
			auto& registry = scene.GetRegistry();
			if (registry.all_of<PrefabInstanceComponent>(entity)) {
				const auto& instance = registry.get<PrefabInstanceComponent>(entity);
				if (instance.SourceEntityId != 0) {
					return instance.SourceEntityId;
				}
			}
			if (registry.all_of<UUIDComponent>(entity)) {
				return static_cast<uint64_t>(registry.get<UUIDComponent>(entity).Id);
			}
			return 0;
		}

		std::vector<EntityHandle> CollectPrefabInstanceSubtree(Scene& scene, EntityHandle root, uint64_t prefabGuid) {
			std::vector<EntityHandle> entities;
			auto& registry = scene.GetRegistry();
			std::unordered_set<uint32_t> visited;

			std::function<void(EntityHandle)> visit = [&](EntityHandle entity) {
				if (entity == entt::null || !registry.valid(entity)) {
					return;
				}

				const uint32_t key = static_cast<uint32_t>(entity);
				if (!visited.insert(key).second) {
					return;
				}

				if (entity != root
					&& (scene.GetEntityOrigin(entity) != EntityOrigin::Prefab
						|| static_cast<uint64_t>(scene.GetPrefabGUID(entity)) != prefabGuid)) {
					return;
				}

				entities.push_back(entity);
				if (!registry.all_of<HierarchyComponent>(entity)) {
					return;
				}

				const auto children = registry.get<HierarchyComponent>(entity).Children;
				for (EntityHandle child : children) {
					visit(child);
				}
			};

			visit(root);
			return entities;
		}

		Value SerializePrefabComparableEntity(
			Scene& scene,
			EntityHandle entity,
			const std::unordered_map<uint32_t, uint64_t>& sourceIds) {
			Value entityValue = Detail::SerializeEntity(scene, entity);
			RemovePrefabRuntimeIdentityMembers(entityValue);

			const auto sourceIt = sourceIds.find(static_cast<uint32_t>(entity));
			const uint64_t sourceId = sourceIt != sourceIds.end() ? sourceIt->second : 0;
			if (sourceId != 0) {
				entityValue.AddMember("uuid", Value(std::to_string(sourceId)));
			}

			auto& registry = scene.GetRegistry();
			EntityHandle parent = entt::null;
			if (registry.all_of<HierarchyComponent>(entity)) {
				parent = registry.get<HierarchyComponent>(entity).Parent;
			}

			const auto parentIt = sourceIds.find(static_cast<uint32_t>(parent));
			if (parentIt != sourceIds.end() && parentIt->second != 0) {
				entityValue.AddMember("parentUuid", Value(std::to_string(parentIt->second)));
			}
			else {
				RemoveObjectMember(entityValue, "parentUuid");
			}

			return entityValue;
		}

		void BuildPrefabInstanceOverrideSet(
			Scene& scene,
			EntityHandle root,
			const PrefabDefinition& sourceDefinition,
			Value& outRootOverrides,
			Value& outEntityOverrides) {
			outRootOverrides = Value::MakeObject();
			outEntityOverrides = Value::MakeObject();
			if (sourceDefinition.Entities.empty()) {
				return;
			}

			const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(root));
			const std::vector<EntityHandle> instanceEntities = CollectPrefabInstanceSubtree(scene, root, prefabGuid);
			if (instanceEntities.empty()) {
				return;
			}

			std::unordered_map<uint32_t, uint64_t> sourceIds;
			sourceIds.reserve(instanceEntities.size());
			for (EntityHandle entity : instanceEntities) {
				sourceIds[static_cast<uint32_t>(entity)] = GetPrefabInstanceSourceId(scene, entity);
			}

			Value currentRoot = SerializePrefabComparableEntity(scene, root, sourceIds);
			Value sourceRoot = sourceDefinition.Entities.front();
			RemoveEntityComparisonIdentityMembers(currentRoot);
			RemoveEntityComparisonIdentityMembers(sourceRoot);
			BuildOverridePatch(sourceRoot, currentRoot, {}, outRootOverrides);

			const auto sourceById = BuildSourceEntityMap(sourceDefinition);
			for (EntityHandle entity : instanceEntities) {
				const uint64_t sourceId = sourceIds[static_cast<uint32_t>(entity)];
				if (sourceId == 0) {
					continue;
				}

				const auto sourceIt = sourceById.find(sourceId);
				if (sourceIt == sourceById.end() || !sourceIt->second) {
					continue;
				}

				Value currentValue = SerializePrefabComparableEntity(scene, entity, sourceIds);
				Value sourceValue = *sourceIt->second;
				RemoveEntityComparisonIdentityMembers(currentValue);
				RemoveEntityComparisonIdentityMembers(sourceValue);

				Value entityOverrides = Value::MakeObject();
				BuildOverridePatch(sourceValue, currentValue, {}, entityOverrides);
				if (!entityOverrides.GetObject().empty()) {
					outEntityOverrides.AddMember(std::to_string(sourceId), std::move(entityOverrides));
				}
			}
		}

		bool LoadPrefabEntityValue(uint64_t prefabGuid, Value& outEntityValue) {
			PrefabDefinition definition;
			if (!LoadPrefabDefinition(prefabGuid, definition) || definition.Entities.empty()) {
				return false;
			}

			outEntityValue = definition.Entities.front();
			return true;
		}

		bool LoadPrefabDefinition(uint64_t prefabGuid, PrefabDefinition& outDefinition) {
			const std::string prefabPath = AssetRegistry::ResolvePath(prefabGuid);
			if (prefabPath.empty() || !File::Exists(prefabPath)) {
				return false;
			}

			Value root;
			std::string parseError;
			if (!Json::TryParse(File::ReadAllText(prefabPath), root, &parseError) || !root.IsObject()) {
				AIM_CORE_WARN_TAG("SceneSerializer", "Failed to parse prefab {}: {}", prefabPath, parseError);
				return false;
			}

			if (ReadPrefabDefinitionFromRoot(root, outDefinition)) {
				return true;
			}

			if (const Value* prefabRefValue = root.FindMember("PrefabRef")) {
				uint64_t basePrefabGuid = 0;
				if (prefabRefValue->IsString()) {
					const std::string prefabRef = prefabRefValue->AsStringOr();
					try {
						basePrefabGuid = static_cast<uint64_t>(std::stoull(prefabRef));
					}
					catch (...) {
						basePrefabGuid = AssetRegistry::GetOrCreateAssetUUID(prefabRef);
					}
				}
				else {
					basePrefabGuid = prefabRefValue->AsUInt64Or(0);
				}

				if (basePrefabGuid != 0 && LoadPrefabDefinition(basePrefabGuid, outDefinition)) {
					ApplyEntityOverrides(outDefinition, root);
					return true;
				}
			}

			return false;
		}

		uint64_t ResolvePrefabGuid(const Value& entityValue) {
			uint64_t prefabGuid = GetUInt64Member(entityValue, "PrefabGUID", 0);
			if (prefabGuid != 0) {
				return prefabGuid;
			}

			const std::string prefabRef = GetStringMember(entityValue, "PrefabRef");
			if (!prefabRef.empty()) {
				return AssetRegistry::GetOrCreateAssetUUID(prefabRef);
			}

			return 0;
		}

		void ApplyOverrides(Value& prefabEntityValue, const Value& instanceValue) {
			if (const Value* overrides = GetObjectMember(instanceValue, "Overrides")) {
				for (const auto& [path, overrideValue] : overrides->GetObject()) {
					ApplyOverridePath(prefabEntityValue, path, overrideValue);
				}
				return;
			}

			if (const Value* overrideArray = GetArrayMember(instanceValue, "Overrides")) {
				for (const Value& overrideEntry : overrideArray->GetArray()) {
					if (!overrideEntry.IsObject()) {
						continue;
					}

					const std::string componentName = GetStringMember(overrideEntry, "Component");
					const std::string fieldName = GetStringMember(overrideEntry, "Field");
					const Value* value = overrideEntry.FindMember("Value");
					if (componentName.empty() || fieldName.empty() || !value) {
						continue;
					}

					ApplyOverridePath(prefabEntityValue, componentName + "." + fieldName, *value);
				}
			}
		}
	} // namespace

	bool SceneSerializer::LoadFromFile(Scene& scene, const std::string& path) {
		try {
			if (!File::Exists(path)) {
				AIM_CORE_WARN_TAG("SceneSerializer", "Scene file not found: {}", path);
				return false;
			}

			const std::string json = File::ReadAllText(path);
			if (json.empty()) {
				AIM_CORE_WARN_TAG("SceneSerializer", "Scene file is empty: {}", path);
				return false;
			}

			Value root;
			std::string parseError;
			if (!Json::TryParse(json, root, &parseError) || !root.IsObject()) {
				AIM_CORE_ERROR_TAG("SceneSerializer", "Failed to parse scene JSON {}: {}", path, parseError);
				return false;
			}

			return DeserializeScene(scene, root, path);
		}
		catch (const std::exception& exception) {
			AIM_CORE_ERROR_TAG("SceneSerializer", "Load failed: {}", exception.what());
			return false;
		}
	}

	bool SceneSerializer::DeserializeScene(Scene& scene, const Json::Value& root, std::string_view source) {
		if (!root.IsObject()) {
			AIM_CORE_ERROR_TAG("SceneSerializer", "DeserializeScene requires an object root");
			return false;
		}

		const int version = GetIntMember(root, "version", 1);
		if (version > SCENE_FORMAT_VERSION) {
			AIM_CORE_WARN_TAG(
				"SceneSerializer",
				"Scene version {} is newer than supported ({})",
				version,
				SCENE_FORMAT_VERSION);
		}

		const std::string sourcePath(source);
		const std::string serializedName = GetStringMember(root, "name");
		// Prefer the serialized name when present â€” file-stem fallback is only for
		// legacy scenes that never wrote a "name" field. Previously the file stem
		// always won, making the serialized "name" a write-only field.
		if (!serializedName.empty()) {
			scene.SetName(serializedName);
		}
		else if (!sourcePath.empty()) {
			scene.SetName(std::filesystem::path(sourcePath).stem().string());
		}

		const uint64_t sceneId = GetUInt64Member(root, "sceneId", 0);
		if (sceneId != 0) {
			scene.SetSceneId(UUID(sceneId));
		}

		scene.ClearEntities();
		scene.ClearGameSystems();

		if (const Value* systemsValue = GetArrayMember(root, "systems")) {
			for (const Value& systemValue : systemsValue->GetArray()) {
				const std::string className = systemValue.AsStringOr();
				if (!className.empty()) {
					scene.AddGameSystem(className);
				}
			}
		}

		// Two-pass entity load: first create every entity (so cross-references
		// resolve), then wire parent-child links from each entity's
		// `parentUuid` JSON field. Doing it inline doesn't work because a
		// child can be serialized before its parent.
		std::vector<std::pair<EntityHandle, uint64_t>> pendingParents;
		if (const Value* entitiesValue = GetArrayMember(root, "entities")) {
			for (const Value& entityValue : entitiesValue->GetArray()) {
				if (!entityValue.IsObject()) {
					continue;
				}
				EntityHandle handle = DeserializeEntity(scene, entityValue);
				if (handle == entt::null) continue;

				const std::string parentUuidStr = GetStringMember(entityValue, "parentUuid", std::string{});
				if (!parentUuidStr.empty()) {
					try {
						const uint64_t parentUuid = std::stoull(parentUuidStr);
						if (parentUuid != 0) pendingParents.emplace_back(handle, parentUuid);
					} catch (...) {
						// Malformed UUID â€” ignore; entity becomes a root.
					}
				}
			}
		}
		else {
			if (!sourcePath.empty()) {
				AIM_CORE_WARN_TAG("SceneSerializer", "No entities array in scene file: {}", sourcePath);
			}
			else {
				AIM_CORE_WARN_TAG("SceneSerializer", "No entities array in scene data");
			}
		}

		if (!pendingParents.empty()) {
			std::unordered_map<uint64_t, EntityHandle> byUuid;
			byUuid.reserve(pendingParents.size() * 2);
			for (auto e : scene.GetRegistry().view<UUIDComponent>()) {
				const uint64_t uuid = static_cast<uint64_t>(scene.GetRegistry().get<UUIDComponent>(e).Id);
				byUuid[uuid] = e;
			}
			for (const auto& [childHandle, parentUuid] : pendingParents) {
				auto it = byUuid.find(parentUuid);
				if (it == byUuid.end() || !scene.IsValid(it->second) || !scene.IsValid(childHandle)) continue;
				Entity child = scene.GetEntity(childHandle);
				Entity parent = scene.GetEntity(it->second);
				child.SetParent(parent);
			}
		}

		if (scene.GetRegistry().view<Camera2DComponent>().size() == 0) {
			EntityHelper::CreateCamera2DEntity(scene);
			AIM_CORE_INFO_TAG("SceneSerializer", "Added default camera (none in scene data)");
		}

		scene.ClearDirty();
		AIM_CORE_INFO_TAG("SceneSerializer", "Loaded scene: {}", scene.GetName());
		return true;
	}

	EntityHandle SceneSerializer::DeserializeEntity(Scene& scene, const Json::Value& entityValue) {
		if (!entityValue.IsObject()) {
			return entt::null;
		}

		const std::string originText = GetStringMember(entityValue, "Origin", "Scene");
		const EntityOrigin origin = EntityOriginFromString(originText);
		if (origin == EntityOrigin::Runtime) {
			return entt::null;
		}
		if (origin == EntityOrigin::Prefab) {
			return DeserializePrefabInstance(scene, entityValue);
		}

		return DeserializeFullEntity(scene, entityValue, EntityOrigin::Scene);
	}

	EntityHandle SceneSerializer::DeserializeFullEntity(
		Scene& scene,
		const Json::Value& entityValue,
		EntityOrigin origin,
		uint64_t prefabGuid) {
		if (!entityValue.IsObject()) {
			return entt::null;
		}

		const std::string name = GetStringMember(entityValue, "name", "Entity");
		const EntityHandle entity = scene.CreateEntity(name).GetHandle();

		uint64_t savedSceneGuid = GetUInt64Member(entityValue, "SceneGUID", 0);
		if (savedSceneGuid == 0) {
			savedSceneGuid = GetUInt64Member(entityValue, "uuid", 0);
		}
		const uint64_t savedRuntimeId = origin == EntityOrigin::Prefab
			? 0
			: GetUInt64Member(entityValue, "RuntimeID", 0);
		scene.SetEntityMetaData(
			entity,
			origin,
			AssetGUID(prefabGuid),
			AssetGUID(savedSceneGuid),
			savedRuntimeId);

		if (origin == EntityOrigin::Prefab && scene.HasComponent<PrefabInstanceComponent>(entity)) {
			scene.GetComponent<PrefabInstanceComponent>(entity).SourceEntityId = savedSceneGuid;
		}

		if (const Value* transformValue = GetObjectMember(entityValue, "Transform2D")) {
			auto& transform = scene.GetComponent<Transform2DComponent>(entity);
			// Local* are the authored fields; "posX/posY/rotation/scaleX
			// /scaleY" is the legacy on-disk schema. Old scenes only have the
			// legacy keys, so we treat them as Local for unparented entities
			// (the case before hierarchy existed) â€” TransformHierarchySystem
			// composes the World snapshot from there on the next pass.
			transform.LocalPosition.x = GetFloatMember(*transformValue, "posX", 0.0f);
			transform.LocalPosition.y = GetFloatMember(*transformValue, "posY", 0.0f);
			transform.LocalRotation = GetFloatMember(*transformValue, "rotation", 0.0f);
			transform.LocalScale.x = GetFloatMember(*transformValue, "scaleX", 1.0f);
			transform.LocalScale.y = GetFloatMember(*transformValue, "scaleY", 1.0f);
			// Seed Position/Scale/Rotation with the same values so anything
			// that reads them before TransformHierarchySystem runs (e.g. the
			// physics body construct hooks fired during deserialization)
			// observes the authored placement, not the default identity.
			transform.Position = transform.LocalPosition;
			transform.Scale = transform.LocalScale;
			transform.Rotation = transform.LocalRotation;
		}

		if (GetBoolMember(entityValue, "static", false)) {
			scene.AddComponent<StaticTag>(entity);
		}
		if (GetBoolMember(entityValue, "disabled", false)) {
			scene.AddComponent<DisabledTag>(entity);
		}
		if (GetBoolMember(entityValue, "deadly", false)) {
			scene.AddComponent<DeadlyTag>(entity);
		}

		if (const Value* spriteValue = GetObjectMember(entityValue, "SpriteRenderer")) {
			auto& spriteRenderer = scene.AddComponent<SpriteRendererComponent>(entity);
			spriteRenderer.Color.r = GetFloatMember(*spriteValue, "r", 1.0f);
			spriteRenderer.Color.g = GetFloatMember(*spriteValue, "g", 1.0f);
			spriteRenderer.Color.b = GetFloatMember(*spriteValue, "b", 1.0f);
			spriteRenderer.Color.a = GetFloatMember(*spriteValue, "a", 1.0f);
			spriteRenderer.SortingOrder = static_cast<short>(GetIntMember(*spriteValue, "sortOrder", 0));
			spriteRenderer.SortingLayer = static_cast<uint8_t>(GetIntMember(*spriteValue, "sortLayer", 0));
			const Filter filter = static_cast<Filter>(GetIntMember(*spriteValue, "filter", static_cast<int>(Filter::Point)));
			const Wrap wrapU = static_cast<Wrap>(GetIntMember(*spriteValue, "wrapU", static_cast<int>(Wrap::Clamp)));
			const Wrap wrapV = static_cast<Wrap>(GetIntMember(*spriteValue, "wrapV", static_cast<int>(Wrap::Clamp)));
			spriteRenderer.TextureHandle = LoadTextureFromValue(
				*spriteValue,
				"textureAsset",
				"texture",
				filter,
				wrapU,
				wrapV,
				&spriteRenderer.TextureAssetId);
		}

		if (const Value* textValue = GetObjectMember(entityValue, "TextRenderer")) {
			auto& text = scene.AddComponent<TextRendererComponent>(entity);
			text.Text = GetStringMember(*textValue, "text", text.Text);
			const std::string fontAssetStr = GetStringMember(*textValue, "fontAsset", std::string{});
			if (!fontAssetStr.empty()) {
				try {
					text.FontAssetId = UUID(std::stoull(fontAssetStr));
				} catch (...) {
					text.FontAssetId = UUID(0);
				}
			}
			text.FontSize = GetFloatMember(*textValue, "fontSize", text.FontSize);
			text.Color.r = GetFloatMember(*textValue, "r", text.Color.r);
			text.Color.g = GetFloatMember(*textValue, "g", text.Color.g);
			text.Color.b = GetFloatMember(*textValue, "b", text.Color.b);
			text.Color.a = GetFloatMember(*textValue, "a", text.Color.a);
			text.LetterSpacing = GetFloatMember(*textValue, "letterSpacing", text.LetterSpacing);
			text.HAlign = static_cast<TextAlignment>(GetIntMember(*textValue, "alignment", static_cast<int>(text.HAlign)));
			text.SortingOrder = static_cast<int16_t>(GetIntMember(*textValue, "sortOrder", text.SortingOrder));
			text.SortingLayer = static_cast<uint8_t>(GetIntMember(*textValue, "sortLayer", text.SortingLayer));
			text.ResolvedFont = FontHandle{};
		}

		if (const Value* rigidbodyValue = GetObjectMember(entityValue, "Rigidbody2D")) {
			auto& rigidbody = scene.AddComponent<Rigidbody2DComponent>(entity);
			rigidbody.SetBodyType(static_cast<BodyType>(GetIntMember(*rigidbodyValue, "bodyType", static_cast<int>(BodyType::Dynamic))));
			rigidbody.SetGravityScale(GetFloatMember(*rigidbodyValue, "gravityScale", 1.0f));
			rigidbody.SetMass(GetFloatMember(*rigidbodyValue, "mass", 1.0f));
		}

		if (const Value* colliderValue = GetObjectMember(entityValue, "BoxCollider2D")) {
			auto& boxCollider = scene.AddComponent<BoxCollider2DComponent>(entity);
			const Vec2 savedCenter{
				GetFloatMember(*colliderValue, "centerX", 0.0f),
				GetFloatMember(*colliderValue, "centerY", 0.0f)
			};
			boxCollider.SetCenter(savedCenter, scene);

			const auto& transform = scene.GetComponent<Transform2DComponent>(entity);
			const Vec2 savedScale{
				GetFloatMember(*colliderValue, "scaleX", transform.Scale.x),
				GetFloatMember(*colliderValue, "scaleY", transform.Scale.y)
			};
			Vec2 localScale{ 1.0f, 1.0f };
			if (std::fabs(transform.Scale.x) > k_MinScaleAxis) {
				localScale.x = savedScale.x / transform.Scale.x;
			}
			if (std::fabs(transform.Scale.y) > k_MinScaleAxis) {
				localScale.y = savedScale.y / transform.Scale.y;
			}
			boxCollider.SetScale(localScale, scene);
			boxCollider.SetSensor(GetBoolMember(*colliderValue, "sensor", false), scene);
			boxCollider.SetFriction(GetFloatMember(*colliderValue, "friction", boxCollider.GetFriction()));
			boxCollider.SetBounciness(GetFloatMember(*colliderValue, "bounciness", boxCollider.GetBounciness()));
			boxCollider.SetLayer(GetUInt64Member(*colliderValue, "layer", boxCollider.GetLayer()));
			boxCollider.SetRegisterContacts(GetBoolMember(*colliderValue, "registerContacts", boxCollider.CanRegisterContacts()));
		}

		if (const Value* audioValue = GetObjectMember(entityValue, "AudioSource")) {
			auto& audioSource = scene.AddComponent<AudioSourceComponent>(entity);
			audioSource.SetVolume(GetFloatMember(*audioValue, "volume", 1.0f));
			audioSource.SetPitch(GetFloatMember(*audioValue, "pitch", 1.0f));
			audioSource.SetLoop(GetBoolMember(*audioValue, "loop", false));
			audioSource.SetPlayOnAwake(GetBoolMember(*audioValue, "playOnAwake", false));

			UUID audioAssetId = UUID(0);
			const AudioHandle handle = LoadAudioFromValue(*audioValue, "clipAsset", "clip", &audioAssetId);
			if (handle.IsValid()) {
				audioSource.SetAudioHandle(handle, audioAssetId);
			}
		}

		if (const Value* cameraValue = GetObjectMember(entityValue, "Camera2D")) {
			auto& camera = scene.AddComponent<Camera2DComponent>(entity);
			camera.SetOrthographicSize(GetFloatMember(*cameraValue, "orthoSize", 5.0f));
			camera.SetZoom(GetFloatMember(*cameraValue, "zoom", 1.0f));
			camera.SetClearColor(Color(
				GetFloatMember(*cameraValue, "clearR", 0.1f),
				GetFloatMember(*cameraValue, "clearG", 0.1f),
				GetFloatMember(*cameraValue, "clearB", 0.1f),
				GetFloatMember(*cameraValue, "clearA", 1.0f)));
		}

		if (const Value* bodyValue = GetObjectMember(entityValue, "FastBody2D")) {
			auto& body = scene.AddComponent<FastBody2DComponent>(entity);
			body.Type = static_cast<AxiomPhys::BodyType>(GetIntMember(*bodyValue, "type", 1));
			body.Mass = GetFloatMember(*bodyValue, "mass", 1.0f);
			body.UseGravity = GetBoolMember(*bodyValue, "useGravity", true);
			body.BoundaryCheck = GetBoolMember(*bodyValue, "boundaryCheck", false);

			if (body.m_Body) {
				body.m_Body->SetBodyType(body.Type);
				body.m_Body->SetMass(body.Mass);
				body.m_Body->SetGravityEnabled(body.UseGravity);
				body.m_Body->SetBoundaryCheckEnabled(body.BoundaryCheck);
			}
		}

		if (const Value* colliderValue = GetObjectMember(entityValue, "FastBoxCollider2D")) {
			auto& collider = scene.AddComponent<FastBoxCollider2DComponent>(entity);
			collider.HalfExtents = {
				GetFloatMember(*colliderValue, "halfX", 0.5f),
				GetFloatMember(*colliderValue, "halfY", 0.5f)
			};
			if (collider.m_Collider) {
				collider.m_Collider->SetHalfExtents({ collider.HalfExtents.x, collider.HalfExtents.y });
			}
		}

		if (const Value* colliderValue = GetObjectMember(entityValue, "FastCircleCollider2D")) {
			auto& collider = scene.AddComponent<FastCircleCollider2DComponent>(entity);
			collider.Radius = GetFloatMember(*colliderValue, "radius", 0.5f);
			if (collider.m_Collider) {
				collider.m_Collider->SetRadius(collider.Radius);
			}
		}

		if (const Value* particleValue = GetObjectMember(entityValue, "ParticleSystem2D")) {
			auto& particleSystem = scene.AddComponent<ParticleSystem2DComponent>(entity);
			particleSystem.PlayOnAwake = GetBoolMember(*particleValue, "playOnAwake", true);
			particleSystem.ParticleSettings.LifeTime = GetFloatMember(*particleValue, "lifetime", 1.0f);
			particleSystem.ParticleSettings.Speed = GetFloatMember(*particleValue, "speed", 5.0f);
			particleSystem.ParticleSettings.Scale = GetFloatMember(*particleValue, "scale", 1.0f);
			particleSystem.ParticleSettings.Gravity.x = GetFloatMember(*particleValue, "gravityX", 0.0f);
			particleSystem.ParticleSettings.Gravity.y = GetFloatMember(*particleValue, "gravityY", 0.0f);
			particleSystem.ParticleSettings.UseGravity = GetBoolMember(*particleValue, "useGravity", false);
			particleSystem.ParticleSettings.UseRandomColors = GetBoolMember(*particleValue, "useRandomColors", false);
			particleSystem.ParticleSettings.MoveDirection.x = GetFloatMember(*particleValue, "moveDirectionX", 0.0f);
			particleSystem.ParticleSettings.MoveDirection.y = GetFloatMember(*particleValue, "moveDirectionY", 0.0f);
			particleSystem.EmissionSettings.EmitOverTime =
				static_cast<uint16_t>(GetIntMember(*particleValue, "emitOverTime", 10));
			particleSystem.EmissionSettings.RateOverDistance =
				static_cast<uint16_t>(GetIntMember(*particleValue, "rateOverDistance", 0));
			particleSystem.EmissionSettings.EmissionSpace = static_cast<ParticleSystem2DComponent::Space>(
				GetIntMember(*particleValue, "emissionSpace", static_cast<int>(ParticleSystem2DComponent::Space::World)));

			if (GetIntMember(*particleValue, "shapeType", 0) == 0) {
				ParticleSystem2DComponent::CircleParams circle;
				circle.Radius = GetFloatMember(*particleValue, "radius", 1.0f);
				circle.IsOnCircle = GetBoolMember(*particleValue, "isOnCircle", false);
				particleSystem.Shape = circle;
			}
			else {
				ParticleSystem2DComponent::SquareParams square;
				square.HalfExtends.x = GetFloatMember(*particleValue, "halfExtendsX", 1.0f);
				square.HalfExtends.y = GetFloatMember(*particleValue, "halfExtendsY", 1.0f);
				particleSystem.Shape = square;
			}

			particleSystem.RenderingSettings.MaxParticles =
				static_cast<uint32_t>(GetUInt64Member(*particleValue, "maxParticles", 1000));
			particleSystem.RenderingSettings.Color.r = GetFloatMember(*particleValue, "colorR", 1.0f);
			particleSystem.RenderingSettings.Color.g = GetFloatMember(*particleValue, "colorG", 1.0f);
			particleSystem.RenderingSettings.Color.b = GetFloatMember(*particleValue, "colorB", 1.0f);
			particleSystem.RenderingSettings.Color.a = GetFloatMember(*particleValue, "colorA", 1.0f);
			particleSystem.RenderingSettings.SortingOrder =
				static_cast<short>(GetIntMember(*particleValue, "sortOrder", 0));
			particleSystem.RenderingSettings.SortingLayer =
				static_cast<uint8_t>(GetIntMember(*particleValue, "sortLayer", 0));

			UUID textureAssetId = UUID(0);
			const TextureHandle textureHandle = LoadTextureFromValue(
				*particleValue,
				"textureAsset",
				"texture",
				Filter::Point,
				Wrap::Clamp,
				Wrap::Clamp,
				&textureAssetId);
			if (textureHandle.IsValid()) {
				particleSystem.SetTexture(textureHandle, textureAssetId);
			}
		}

		if (const Value* rectValue = GetObjectMember(entityValue, "RectTransform2D")) {
			auto& rectTransform = scene.AddComponent<RectTransform2DComponent>(entity);
			rectTransform.AnchorMin.x        = GetFloatMember(*rectValue, "anchorMinX", 0.5f);
			rectTransform.AnchorMin.y        = GetFloatMember(*rectValue, "anchorMinY", 0.5f);
			rectTransform.AnchorMax.x        = GetFloatMember(*rectValue, "anchorMaxX", 0.5f);
			rectTransform.AnchorMax.y        = GetFloatMember(*rectValue, "anchorMaxY", 0.5f);
			rectTransform.Pivot.x            = GetFloatMember(*rectValue, "pivotX", 0.5f);
			rectTransform.Pivot.y            = GetFloatMember(*rectValue, "pivotY", 0.5f);
			rectTransform.AnchoredPosition.x = GetFloatMember(*rectValue, "posX", 0.0f);
			rectTransform.AnchoredPosition.y = GetFloatMember(*rectValue, "posY", 0.0f);
			// Back-compat: pre-anchor saves used "width"/"height" instead of sizeX/sizeY.
			rectTransform.SizeDelta.x        = GetFloatMember(*rectValue, "sizeX",
				GetFloatMember(*rectValue, "width", 100.0f));
			rectTransform.SizeDelta.y        = GetFloatMember(*rectValue, "sizeY",
				GetFloatMember(*rectValue, "height", 100.0f));
			rectTransform.Rotation           = GetFloatMember(*rectValue, "rotation", 0.0f);
			rectTransform.Scale.x            = GetFloatMember(*rectValue, "scaleX", 1.0f);
			rectTransform.Scale.y            = GetFloatMember(*rectValue, "scaleY", 1.0f);
		}

		if (const Value* imageValue = GetObjectMember(entityValue, "Image")) {
			auto& image = scene.AddComponent<ImageComponent>(entity);
			image.Color.r = GetFloatMember(*imageValue, "r", 1.0f);
			image.Color.g = GetFloatMember(*imageValue, "g", 1.0f);
			image.Color.b = GetFloatMember(*imageValue, "b", 1.0f);
			image.Color.a = GetFloatMember(*imageValue, "a", 1.0f);

			image.TextureHandle = LoadTextureFromValue(
				*imageValue,
				"textureAsset",
				"texture",
				Filter::Point,
				Wrap::Clamp,
				Wrap::Clamp,
				&image.TextureAssetId);
		}

		ScriptComponent* scriptComponent = nullptr;
		auto getOrCreateScriptComponent = [&]() -> ScriptComponent& {
			if (!scriptComponent) {
				if (scene.HasComponent<ScriptComponent>(entity)) {
					scriptComponent = &scene.GetComponent<ScriptComponent>(entity);
				}
				else {
					scriptComponent = &scene.AddComponent<ScriptComponent>(entity);
				}
			}
			return *scriptComponent;
		};

		if (const Value* scriptsValue = GetArrayMember(entityValue, "Scripts")) {
			auto& scripts = getOrCreateScriptComponent();
			for (const Value& scriptValue : scriptsValue->GetArray()) {
				if (scriptValue.IsString()) {
					scripts.AddScript(scriptValue.AsStringOr(), ScriptType::Unknown);
					continue;
				}

				if (!scriptValue.IsObject()) {
					continue;
				}

				std::string className = GetStringMember(scriptValue, "className");
				if (className.empty()) {
					className = GetStringMember(scriptValue, "class");
				}
				if (className.empty()) {
					continue;
				}

				scripts.AddScript(
					className,
					ScriptTypeFromString(GetStringMember(scriptValue, "type")));
			}
		}

		if (const Value* componentsValue = GetArrayMember(entityValue, "ManagedComponents")) {
			auto& components = getOrCreateScriptComponent();
			for (const Value& componentValue : componentsValue->GetArray()) {
				if (componentValue.IsString()) {
					components.AddManagedComponent(componentValue.AsStringOr());
					continue;
				}

				if (!componentValue.IsObject()) {
					continue;
				}

				std::string className = GetStringMember(componentValue, "className");
				if (className.empty()) {
					className = GetStringMember(componentValue, "class");
				}
				if (!className.empty()) {
					components.AddManagedComponent(className);
				}
			}
		}

		if (const Value* fieldsByClass = GetObjectMember(entityValue, "ScriptFields")) {
			if (scriptComponent || scene.HasComponent<ScriptComponent>(entity)) {
				auto& fieldsComponent = getOrCreateScriptComponent();
				for (const auto& [className, fieldsValue] : fieldsByClass->GetObject()) {
					if ((!fieldsComponent.HasScript(className) && !fieldsComponent.HasManagedComponent(className))
						|| !fieldsValue.IsArray()) {
						continue;
					}

					for (const Value& fieldValue : fieldsValue.GetArray()) {
						if (!fieldValue.IsObject()) {
							continue;
						}

						const std::string fieldName = GetStringMember(fieldValue, "name");
						if (fieldName.empty()) {
							continue;
						}

						const Value* valueValue = fieldValue.FindMember("value");
						if (!valueValue) {
							continue;
						}

						fieldsComponent.PendingFieldValues[className + "." + fieldName] =
							ValueToFieldString(*valueValue);
					}
				}
			}
		}

		// Registry-driven deserialize for package components.
		if (Application* app = Application::GetInstance(); app && app->GetSceneManager()) {
			Entity entityWrapper = scene.GetEntity(entity);
			app->GetSceneManager()->GetComponentRegistry().ForEachComponentInfo(
				[&](const std::type_index&, const ComponentInfo& info) {
					if (!info.deserialize || !info.has || !info.add || info.serializedName.empty()) return;
					const Value* compValue = entityValue.FindMember(info.serializedName.c_str());
					if (!compValue) return;
					try {
						if (!info.has(entityWrapper)) {
							info.add(entityWrapper);
						}
						info.deserialize(entityWrapper, *compValue);
					} catch (const std::exception& e) {
						AIM_CORE_ERROR_TAG("SceneSerializer",
							"Package component '{}' deserialize threw: {}",
							info.serializedName, e.what());
					} catch (...) {
						AIM_CORE_ERROR_TAG("SceneSerializer",
							"Package component '{}' deserialize threw an unknown exception",
							info.serializedName);
					}
				});
		}

		return entity;
	}

	EntityHandle SceneSerializer::DeserializeEntityTree(
		Scene& scene,
		const std::vector<Json::Value>& entityValues,
		EntityOrigin origin,
		uint64_t prefabGuid,
		bool preserveSerializedIdentity) {
		if (entityValues.empty()) {
			return entt::null;
		}

		std::vector<std::pair<EntityHandle, uint64_t>> pendingParents;
		std::unordered_map<uint64_t, EntityHandle> entitiesBySourceId;
		pendingParents.reserve(entityValues.size());
		entitiesBySourceId.reserve(entityValues.size());

		EntityHandle root = entt::null;
		for (const Value& sourceEntityValue : entityValues) {
			if (!sourceEntityValue.IsObject()) {
				continue;
			}

			const uint64_t sourceId = GetPrefabSourceId(sourceEntityValue);
			const uint64_t parentId = GetUInt64Member(sourceEntityValue, "parentUuid", 0);

			Value entityValue = sourceEntityValue;
			if (preserveSerializedIdentity) {
				RemovePrefabRuntimeIdentityMembers(entityValue);
			}
			else {
				RemoveEntityIdentityMembers(entityValue);
			}

			EntityHandle handle = DeserializeFullEntity(scene, entityValue, origin, prefabGuid);
			if (handle == entt::null) {
				continue;
			}

			if (root == entt::null) {
				root = handle;
			}

			if (sourceId != 0) {
				entitiesBySourceId[sourceId] = handle;
			}

			if (parentId != 0) {
				pendingParents.emplace_back(handle, parentId);
			}
		}

		for (const auto& [childHandle, parentId] : pendingParents) {
			auto parentIt = entitiesBySourceId.find(parentId);
			if (parentIt == entitiesBySourceId.end()) {
				continue;
			}
			if (!scene.IsValid(childHandle) || !scene.IsValid(parentIt->second)) {
				continue;
			}

			scene.GetEntity(childHandle).SetParent(scene.GetEntity(parentIt->second));
		}

		return root;
	}

	EntityHandle SceneSerializer::DeserializeEntityFromValue(Scene& scene, const Json::Value& entityValue) {
		if (!entityValue.IsObject()) {
			return entt::null;
		}

		PrefabDefinition definition;
		if (ReadPrefabDefinitionFromRoot(entityValue, definition)
			&& (entityValue.FindMember("Entity") || entityValue.FindMember("prefab") || entityValue.FindMember("Entities"))) {
			const bool isClipboardEntity = GetBoolMember(entityValue, "ClipboardEntity", false)
				|| GetBoolMember(entityValue, "Clipboard", false);
			return DeserializeEntityTree(scene, definition.Entities, EntityOrigin::Scene, 0, !isClipboardEntity);
		}

		Value entityCopy = entityValue;
		RemoveEntityIdentityMembers(entityCopy);
		return DeserializeFullEntity(scene, entityCopy, EntityOrigin::Scene);
	}

	bool SceneSerializer::DeserializeComponent(Scene& scene, EntityHandle entity, std::string_view componentName, const Json::Value& componentValue) {
		if (entity == entt::null || !scene.IsValid(entity) || componentName.empty() || !componentValue.IsObject()) {
			return false;
		}

		const std::string component(componentName);

		if (component == "Transform2D") {
			auto& transform = scene.HasComponent<Transform2DComponent>(entity)
				? scene.GetComponent<Transform2DComponent>(entity)
				: scene.AddComponent<Transform2DComponent>(entity);
			// Same dual-write as the full-entity deserialize: load into Local*
			// (authored values) and seed World cache so anything reading
			// transform before TransformHierarchySystem ticks gets a sane value.
			transform.LocalPosition.x = GetFloatMember(componentValue, "posX", 0.0f);
			transform.LocalPosition.y = GetFloatMember(componentValue, "posY", 0.0f);
			transform.LocalRotation = GetFloatMember(componentValue, "rotation", 0.0f);
			transform.LocalScale.x = GetFloatMember(componentValue, "scaleX", 1.0f);
			transform.LocalScale.y = GetFloatMember(componentValue, "scaleY", 1.0f);
			transform.Position = transform.LocalPosition;
			transform.Scale = transform.LocalScale;
			transform.Rotation = transform.LocalRotation;
			transform.MarkDirty();
			return true;
		}

		if (component == "SpriteRenderer") {
			auto& spriteRenderer = scene.HasComponent<SpriteRendererComponent>(entity)
				? scene.GetComponent<SpriteRendererComponent>(entity)
				: scene.AddComponent<SpriteRendererComponent>(entity);
			spriteRenderer.Color.r = GetFloatMember(componentValue, "r", 1.0f);
			spriteRenderer.Color.g = GetFloatMember(componentValue, "g", 1.0f);
			spriteRenderer.Color.b = GetFloatMember(componentValue, "b", 1.0f);
			spriteRenderer.Color.a = GetFloatMember(componentValue, "a", 1.0f);
			spriteRenderer.SortingOrder = static_cast<short>(GetIntMember(componentValue, "sortOrder", 0));
			spriteRenderer.SortingLayer = static_cast<uint8_t>(GetIntMember(componentValue, "sortLayer", 0));
			const Filter filter = static_cast<Filter>(GetIntMember(componentValue, "filter", static_cast<int>(Filter::Point)));
			const Wrap wrapU = static_cast<Wrap>(GetIntMember(componentValue, "wrapU", static_cast<int>(Wrap::Clamp)));
			const Wrap wrapV = static_cast<Wrap>(GetIntMember(componentValue, "wrapV", static_cast<int>(Wrap::Clamp)));
			spriteRenderer.TextureHandle = LoadTextureFromValue(
				componentValue,
				"textureAsset",
				"texture",
				filter,
				wrapU,
				wrapV,
				&spriteRenderer.TextureAssetId);
			return true;
		}

		if (component == "TextRenderer") {
			auto& text = scene.HasComponent<TextRendererComponent>(entity)
				? scene.GetComponent<TextRendererComponent>(entity)
				: scene.AddComponent<TextRendererComponent>(entity);
			text.Text = GetStringMember(componentValue, "text", text.Text);
			const std::string fontAssetStr = GetStringMember(componentValue, "fontAsset", std::string{});
			if (!fontAssetStr.empty()) {
				try {
					text.FontAssetId = UUID(std::stoull(fontAssetStr));
				} catch (...) {
					text.FontAssetId = UUID(0);
				}
			}
			text.FontSize = GetFloatMember(componentValue, "fontSize", text.FontSize);
			text.Color.r = GetFloatMember(componentValue, "r", text.Color.r);
			text.Color.g = GetFloatMember(componentValue, "g", text.Color.g);
			text.Color.b = GetFloatMember(componentValue, "b", text.Color.b);
			text.Color.a = GetFloatMember(componentValue, "a", text.Color.a);
			text.LetterSpacing = GetFloatMember(componentValue, "letterSpacing", text.LetterSpacing);
			text.HAlign = static_cast<TextAlignment>(GetIntMember(componentValue, "alignment", static_cast<int>(text.HAlign)));
			text.SortingOrder = static_cast<int16_t>(GetIntMember(componentValue, "sortOrder", text.SortingOrder));
			text.SortingLayer = static_cast<uint8_t>(GetIntMember(componentValue, "sortLayer", text.SortingLayer));
			text.ResolvedFont = FontHandle{};
			return true;
		}

		if (component == "Rigidbody2D") {
			auto& rigidbody = scene.HasComponent<Rigidbody2DComponent>(entity)
				? scene.GetComponent<Rigidbody2DComponent>(entity)
				: scene.AddComponent<Rigidbody2DComponent>(entity);
			rigidbody.SetBodyType(static_cast<BodyType>(GetIntMember(componentValue, "bodyType", static_cast<int>(BodyType::Dynamic))));
			rigidbody.SetGravityScale(GetFloatMember(componentValue, "gravityScale", 1.0f));
			rigidbody.SetMass(GetFloatMember(componentValue, "mass", 1.0f));
			return true;
		}

		if (component == "BoxCollider2D") {
			auto& boxCollider = scene.HasComponent<BoxCollider2DComponent>(entity)
				? scene.GetComponent<BoxCollider2DComponent>(entity)
				: scene.AddComponent<BoxCollider2DComponent>(entity);
			const Vec2 savedCenter{
				GetFloatMember(componentValue, "centerX", 0.0f),
				GetFloatMember(componentValue, "centerY", 0.0f)
			};
			boxCollider.SetCenter(savedCenter, scene);

			const auto& transform = scene.GetComponent<Transform2DComponent>(entity);
			const Vec2 savedScale{
				GetFloatMember(componentValue, "scaleX", transform.Scale.x),
				GetFloatMember(componentValue, "scaleY", transform.Scale.y)
			};
			Vec2 localScale{ 1.0f, 1.0f };
			if (std::fabs(transform.Scale.x) > k_MinScaleAxis) {
				localScale.x = savedScale.x / transform.Scale.x;
			}
			if (std::fabs(transform.Scale.y) > k_MinScaleAxis) {
				localScale.y = savedScale.y / transform.Scale.y;
			}
			boxCollider.SetScale(localScale, scene);
			boxCollider.SetSensor(GetBoolMember(componentValue, "sensor", false), scene);
			boxCollider.SetFriction(GetFloatMember(componentValue, "friction", boxCollider.GetFriction()));
			boxCollider.SetBounciness(GetFloatMember(componentValue, "bounciness", boxCollider.GetBounciness()));
			boxCollider.SetLayer(GetUInt64Member(componentValue, "layer", boxCollider.GetLayer()));
			boxCollider.SetRegisterContacts(GetBoolMember(componentValue, "registerContacts", boxCollider.CanRegisterContacts()));
			return true;
		}

		if (component == "AudioSource") {
			auto& audioSource = scene.HasComponent<AudioSourceComponent>(entity)
				? scene.GetComponent<AudioSourceComponent>(entity)
				: scene.AddComponent<AudioSourceComponent>(entity);
			audioSource.SetVolume(GetFloatMember(componentValue, "volume", 1.0f));
			audioSource.SetPitch(GetFloatMember(componentValue, "pitch", 1.0f));
			audioSource.SetLoop(GetBoolMember(componentValue, "loop", false));
			audioSource.SetPlayOnAwake(GetBoolMember(componentValue, "playOnAwake", false));

			UUID audioAssetId = UUID(0);
			const AudioHandle handle = LoadAudioFromValue(componentValue, "clipAsset", "clip", &audioAssetId);
			audioSource.SetAudioHandle(handle, audioAssetId);
			return true;
		}

		if (component == "Camera2D") {
			auto& camera = scene.HasComponent<Camera2DComponent>(entity)
				? scene.GetComponent<Camera2DComponent>(entity)
				: scene.AddComponent<Camera2DComponent>(entity);
			camera.SetOrthographicSize(GetFloatMember(componentValue, "orthoSize", 5.0f));
			camera.SetZoom(GetFloatMember(componentValue, "zoom", 1.0f));
			camera.SetClearColor(Color(
				GetFloatMember(componentValue, "clearR", 0.1f),
				GetFloatMember(componentValue, "clearG", 0.1f),
				GetFloatMember(componentValue, "clearB", 0.1f),
				GetFloatMember(componentValue, "clearA", 1.0f)));
			return true;
		}

		if (component == "FastBody2D") {
			auto& body = scene.HasComponent<FastBody2DComponent>(entity)
				? scene.GetComponent<FastBody2DComponent>(entity)
				: scene.AddComponent<FastBody2DComponent>(entity);
			body.Type = static_cast<AxiomPhys::BodyType>(GetIntMember(componentValue, "type", 1));
			body.Mass = GetFloatMember(componentValue, "mass", 1.0f);
			body.UseGravity = GetBoolMember(componentValue, "useGravity", true);
			body.BoundaryCheck = GetBoolMember(componentValue, "boundaryCheck", false);

			if (body.m_Body) {
				body.m_Body->SetBodyType(body.Type);
				body.m_Body->SetMass(body.Mass);
				body.m_Body->SetGravityEnabled(body.UseGravity);
				body.m_Body->SetBoundaryCheckEnabled(body.BoundaryCheck);
			}
			return true;
		}

		if (component == "FastBoxCollider2D") {
			auto& collider = scene.HasComponent<FastBoxCollider2DComponent>(entity)
				? scene.GetComponent<FastBoxCollider2DComponent>(entity)
				: scene.AddComponent<FastBoxCollider2DComponent>(entity);
			collider.HalfExtents = {
				GetFloatMember(componentValue, "halfX", 0.5f),
				GetFloatMember(componentValue, "halfY", 0.5f)
			};
			if (collider.m_Collider) {
				collider.m_Collider->SetHalfExtents({ collider.HalfExtents.x, collider.HalfExtents.y });
			}
			return true;
		}

		if (component == "FastCircleCollider2D") {
			auto& collider = scene.HasComponent<FastCircleCollider2DComponent>(entity)
				? scene.GetComponent<FastCircleCollider2DComponent>(entity)
				: scene.AddComponent<FastCircleCollider2DComponent>(entity);
			collider.Radius = GetFloatMember(componentValue, "radius", 0.5f);
			if (collider.m_Collider) {
				collider.m_Collider->SetRadius(collider.Radius);
			}
			return true;
		}

		if (component == "ParticleSystem2D") {
			auto& particleSystem = scene.HasComponent<ParticleSystem2DComponent>(entity)
				? scene.GetComponent<ParticleSystem2DComponent>(entity)
				: scene.AddComponent<ParticleSystem2DComponent>(entity);
			particleSystem.PlayOnAwake = GetBoolMember(componentValue, "playOnAwake", true);
			particleSystem.ParticleSettings.LifeTime = GetFloatMember(componentValue, "lifetime", 1.0f);
			particleSystem.ParticleSettings.Speed = GetFloatMember(componentValue, "speed", 5.0f);
			particleSystem.ParticleSettings.Scale = GetFloatMember(componentValue, "scale", 1.0f);
			particleSystem.ParticleSettings.Gravity.x = GetFloatMember(componentValue, "gravityX", 0.0f);
			particleSystem.ParticleSettings.Gravity.y = GetFloatMember(componentValue, "gravityY", 0.0f);
			particleSystem.ParticleSettings.UseGravity = GetBoolMember(componentValue, "useGravity", false);
			particleSystem.ParticleSettings.UseRandomColors = GetBoolMember(componentValue, "useRandomColors", false);
			particleSystem.ParticleSettings.MoveDirection.x = GetFloatMember(componentValue, "moveDirectionX", 0.0f);
			particleSystem.ParticleSettings.MoveDirection.y = GetFloatMember(componentValue, "moveDirectionY", 0.0f);
			particleSystem.EmissionSettings.EmitOverTime =
				static_cast<uint16_t>(GetIntMember(componentValue, "emitOverTime", 10));
			particleSystem.EmissionSettings.RateOverDistance =
				static_cast<uint16_t>(GetIntMember(componentValue, "rateOverDistance", 0));
			particleSystem.EmissionSettings.EmissionSpace = static_cast<ParticleSystem2DComponent::Space>(
				GetIntMember(componentValue, "emissionSpace", static_cast<int>(ParticleSystem2DComponent::Space::World)));

			if (GetIntMember(componentValue, "shapeType", 0) == 0) {
				ParticleSystem2DComponent::CircleParams circle;
				circle.Radius = GetFloatMember(componentValue, "radius", 1.0f);
				circle.IsOnCircle = GetBoolMember(componentValue, "isOnCircle", false);
				particleSystem.Shape = circle;
			}
			else {
				ParticleSystem2DComponent::SquareParams square;
				square.HalfExtends.x = GetFloatMember(componentValue, "halfExtendsX", 1.0f);
				square.HalfExtends.y = GetFloatMember(componentValue, "halfExtendsY", 1.0f);
				particleSystem.Shape = square;
			}

			particleSystem.RenderingSettings.MaxParticles =
				static_cast<uint32_t>(GetUInt64Member(componentValue, "maxParticles", 1000));
			particleSystem.RenderingSettings.Color.r = GetFloatMember(componentValue, "colorR", 1.0f);
			particleSystem.RenderingSettings.Color.g = GetFloatMember(componentValue, "colorG", 1.0f);
			particleSystem.RenderingSettings.Color.b = GetFloatMember(componentValue, "colorB", 1.0f);
			particleSystem.RenderingSettings.Color.a = GetFloatMember(componentValue, "colorA", 1.0f);
			particleSystem.RenderingSettings.SortingOrder =
				static_cast<short>(GetIntMember(componentValue, "sortOrder", 0));
			particleSystem.RenderingSettings.SortingLayer =
				static_cast<uint8_t>(GetIntMember(componentValue, "sortLayer", 0));

			UUID textureAssetId = UUID(0);
			const TextureHandle textureHandle = LoadTextureFromValue(
				componentValue,
				"textureAsset",
				"texture",
				Filter::Point,
				Wrap::Clamp,
				Wrap::Clamp,
				&textureAssetId);
			particleSystem.SetTexture(textureHandle, textureAssetId);
			return true;
		}

		if (component == "RectTransform2D") {
			auto& rectTransform = scene.HasComponent<RectTransform2DComponent>(entity)
				? scene.GetComponent<RectTransform2DComponent>(entity)
				: scene.AddComponent<RectTransform2DComponent>(entity);
			rectTransform.AnchorMin.x        = GetFloatMember(componentValue, "anchorMinX", 0.5f);
			rectTransform.AnchorMin.y        = GetFloatMember(componentValue, "anchorMinY", 0.5f);
			rectTransform.AnchorMax.x        = GetFloatMember(componentValue, "anchorMaxX", 0.5f);
			rectTransform.AnchorMax.y        = GetFloatMember(componentValue, "anchorMaxY", 0.5f);
			rectTransform.Pivot.x            = GetFloatMember(componentValue, "pivotX", 0.5f);
			rectTransform.Pivot.y            = GetFloatMember(componentValue, "pivotY", 0.5f);
			rectTransform.AnchoredPosition.x = GetFloatMember(componentValue, "posX", 0.0f);
			rectTransform.AnchoredPosition.y = GetFloatMember(componentValue, "posY", 0.0f);
			rectTransform.SizeDelta.x        = GetFloatMember(componentValue, "sizeX",
				GetFloatMember(componentValue, "width", 100.0f));
			rectTransform.SizeDelta.y        = GetFloatMember(componentValue, "sizeY",
				GetFloatMember(componentValue, "height", 100.0f));
			rectTransform.Rotation           = GetFloatMember(componentValue, "rotation", 0.0f);
			rectTransform.Scale.x            = GetFloatMember(componentValue, "scaleX", 1.0f);
			rectTransform.Scale.y            = GetFloatMember(componentValue, "scaleY", 1.0f);
			return true;
		}

		if (component == "Image") {
			auto& image = scene.HasComponent<ImageComponent>(entity)
				? scene.GetComponent<ImageComponent>(entity)
				: scene.AddComponent<ImageComponent>(entity);
			image.Color.r = GetFloatMember(componentValue, "r", 1.0f);
			image.Color.g = GetFloatMember(componentValue, "g", 1.0f);
			image.Color.b = GetFloatMember(componentValue, "b", 1.0f);
			image.Color.a = GetFloatMember(componentValue, "a", 1.0f);

			image.TextureHandle = LoadTextureFromValue(
				componentValue,
				"textureAsset",
				"texture",
				Filter::Point,
				Wrap::Clamp,
				Wrap::Clamp,
				&image.TextureAssetId);
			return true;
		}

		// Unknown built-in: try registry for a package component with matching serializedName.
		if (Application* app = Application::GetInstance(); app && app->GetSceneManager()) {
			Entity entityWrapper = scene.GetEntity(entity);
			bool handled = false;
			app->GetSceneManager()->GetComponentRegistry().ForEachComponentInfo(
				[&](const std::type_index&, const ComponentInfo& info) {
					if (handled) return;
					if (!info.deserialize || !info.has || !info.add) return;
					if (info.serializedName != componentName) return;
					try {
						if (!info.has(entityWrapper)) {
							info.add(entityWrapper);
						}
						info.deserialize(entityWrapper, componentValue);
						handled = true;
					} catch (const std::exception& e) {
						AIM_CORE_ERROR_TAG("SceneSerializer",
							"Package component '{}' deserialize (single) threw: {}",
							info.serializedName, e.what());
					} catch (...) {
						AIM_CORE_ERROR_TAG("SceneSerializer",
							"Package component '{}' deserialize (single) threw an unknown exception",
							info.serializedName);
					}
				});
			if (handled) return true;
		}

		return false;
	}

	bool SceneSerializer::ResetComponent(Scene& scene, EntityHandle entity, std::string_view componentName) {
		Value defaults = Value::MakeObject();
		return DeserializeComponent(scene, entity, componentName, defaults);
	}

	EntityHandle SceneSerializer::DeserializePrefabInstance(Scene& scene, const Json::Value& entityValue) {
		const uint64_t prefabGuid = ResolvePrefabGuid(entityValue);
		if (prefabGuid == 0) {
			AIM_CORE_WARN_TAG("SceneSerializer", "Prefab instance missing PrefabGUID");
			return entt::null;
		}

		PrefabDefinition definition;
		if (!LoadPrefabDefinition(prefabGuid, definition)) {
			AIM_CORE_WARN_TAG("SceneSerializer", "Could not load prefab {}", prefabGuid);
			return entt::null;
		}

		ApplyEntityOverrides(definition, entityValue);
		const EntityHandle root = DeserializeEntityTree(scene, definition.Entities, EntityOrigin::Prefab, prefabGuid);
		const uint64_t instanceUuid = GetUInt64Member(entityValue, "uuid", 0);
		if (root != entt::null && instanceUuid != 0 && scene.HasComponent<UUIDComponent>(root)) {
			scene.GetComponent<UUIDComponent>(root).Id = UUID(instanceUuid);
		}
		return root;
	}

	EntityHandle SceneSerializer::InstantiatePrefab(Scene& scene, uint64_t prefabGuid) {
		if (prefabGuid == 0 || AssetRegistry::GetKind(prefabGuid) != AssetKind::Prefab) {
			return entt::null;
		}

		PrefabDefinition definition;
		if (!LoadPrefabDefinition(prefabGuid, definition)) {
			return entt::null;
		}

		return DeserializeEntityTree(scene, definition.Entities, EntityOrigin::Prefab, prefabGuid);
	}

	bool SceneSerializer::ApplyPrefabInstanceOverrides(Scene& scene, EntityHandle entity) {
		if (entity == entt::null || !scene.IsValid(entity) || scene.GetEntityOrigin(entity) != EntityOrigin::Prefab) {
			return false;
		}

		const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(entity));
		const std::string prefabPath = AssetRegistry::ResolvePath(prefabGuid);
		if (prefabGuid == 0 || prefabPath.empty()) {
			return false;
		}

		try {
			if (!SaveEntityToFile(scene, entity, prefabPath)) {
				return false;
			}
			AssetRegistry::MarkDirty();
			AssetRegistry::Sync();
			scene.MarkDirty();
			return true;
		}
		catch (const std::exception& exception) {
			AIM_CORE_ERROR_TAG("SceneSerializer", "ApplyPrefabInstanceOverrides failed: {}", exception.what());
			return false;
		}
	}

	EntityHandle SceneSerializer::RevertPrefabInstanceOverride(
		Scene& scene,
		EntityHandle entity,
		const std::string& overridePath) {
		if (entity == entt::null || !scene.IsValid(entity) || scene.GetEntityOrigin(entity) != EntityOrigin::Prefab) {
			return entt::null;
		}

		const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(entity));
		Value prefabEntityValue;
		if (prefabGuid == 0 || !LoadPrefabEntityValue(prefabGuid, prefabEntityValue)) {
			return entt::null;
		}

		Value replacementValue;
		if (overridePath.empty()) {
			replacementValue = prefabEntityValue;
		}
		else {
			replacementValue = SerializeEntityFull(scene, entity);
			RemoveEntityIdentityMembers(replacementValue);

			const Value* baseValue = GetOverrideValueAtPath(prefabEntityValue, overridePath);
			if (!baseValue || !ApplyOverridePath(replacementValue, overridePath, *baseValue)) {
				return entt::null;
			}
		}

		RemoveEntityIdentityMembers(replacementValue);
		EntityHandle replacement = DeserializeFullEntity(scene, replacementValue, EntityOrigin::Prefab, prefabGuid);
		if (replacement == entt::null) {
			return entt::null;
		}

		scene.DestroyEntity(entity);
		scene.MarkDirty();
		return replacement;
	}

	EntityHandle SceneSerializer::RefreshPrefabInstance(Scene& scene, EntityHandle existing,
		const Json::Value& previousSourceEntityValue) {
		if (existing == entt::null || !scene.IsValid(existing)) return entt::null;
		if (scene.GetEntityOrigin(existing) != EntityOrigin::Prefab) return entt::null;

		const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(existing));
		if (prefabGuid == 0) return entt::null;

		// Orphaned prefab: leave instance untouched rather than corrupting it.
		PrefabDefinition newSource;
		if (!LoadPrefabDefinition(prefabGuid, newSource)) {
			return existing;
		}

		// Diff against OLD source to capture per-field overrides the user kept across the edit.
		PrefabDefinition oldSource;
		if (!ReadPrefabDefinitionFromRoot(previousSourceEntityValue, oldSource)) {
			oldSource.Entities.push_back(previousSourceEntityValue);
		}

		Value overrides;
		Value entityOverrides;
		BuildPrefabInstanceOverrideSet(scene, existing, oldSource, overrides, entityOverrides);

		// Wrapper shape matches ApplyOverrides â€” object with "Overrides" member keyed by dot-path.
		Value overridesWrapper = Value::MakeObject();
		overridesWrapper.AddMember("Overrides", std::move(overrides));
		if (!entityOverrides.GetObject().empty()) {
			overridesWrapper.AddMember("EntityOverrides", std::move(entityOverrides));
		}
		ApplyEntityOverrides(newSource, overridesWrapper);

		// DeserializeEntityTree preserves Origin and PrefabGUID via the explicit args.
		scene.DestroyEntity(existing);
		return DeserializeEntityTree(scene, newSource.Entities, EntityOrigin::Prefab, prefabGuid);
	}

	bool SceneSerializer::ComputeInstanceOverrides(Scene& scene, EntityHandle entity, Json::Value& outOverrides) {
		outOverrides = Json::Value::MakeObject();
		if (entity == entt::null || !scene.IsValid(entity)) return false;
		if (scene.GetEntityOrigin(entity) != EntityOrigin::Prefab) return false;

		const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(entity));
		if (prefabGuid == 0) return false;

		PrefabDefinition sourceDefinition;
		if (!LoadPrefabDefinition(prefabGuid, sourceDefinition) || sourceDefinition.Entities.empty()) return false;

		const uint64_t sourceId = GetPrefabInstanceSourceId(scene, entity);
		const Value* sourceValue = nullptr;
		if (sourceId != 0) {
			const auto sourceById = BuildSourceEntityMap(sourceDefinition);
			const auto sourceIt = sourceById.find(sourceId);
			if (sourceIt != sourceById.end()) {
				sourceValue = sourceIt->second;
			}
		}
		if (!sourceValue) {
			sourceValue = &sourceDefinition.Entities.front();
		}

		std::unordered_map<uint32_t, uint64_t> sourceIds;
		sourceIds[static_cast<uint32_t>(entity)] = sourceId;
		Value currentInstance = SerializePrefabComparableEntity(scene, entity, sourceIds);
		Value sourceComparable = *sourceValue;
		RemoveEntityComparisonIdentityMembers(sourceComparable);
		RemoveEntityComparisonIdentityMembers(currentInstance);

		BuildOverridePatch(sourceComparable, currentInstance, {}, outOverrides);
		return true;
	}

	EntityHandle SceneSerializer::LoadEntityFromFile(Scene& scene, const std::string& path) {
		try {
			if (!File::Exists(path)) {
				AIM_CORE_WARN_TAG("SceneSerializer", "Prefab file not found: {}", path);
				return entt::null;
			}

			const uint64_t prefabGuid = AssetRegistry::GetOrCreateAssetUUID(path);
			if (prefabGuid != 0 && AssetRegistry::GetKind(prefabGuid) == AssetKind::Prefab) {
				EntityHandle instance = InstantiatePrefab(scene, prefabGuid);
				if (instance != entt::null) {
					return instance;
				}
			}

			const std::string json = File::ReadAllText(path);
			if (json.empty()) {
				AIM_CORE_WARN_TAG("SceneSerializer", "Prefab file is empty: {}", path);
				return entt::null;
			}

			Value root;
			std::string parseError;
			if (!Json::TryParse(json, root, &parseError) || !root.IsObject()) {
				AIM_CORE_ERROR_TAG("SceneSerializer", "Failed to parse prefab JSON {}: {}", path, parseError);
				return entt::null;
			}

			PrefabDefinition definition;
			if (!ReadPrefabDefinitionFromRoot(root, definition)) {
				AIM_CORE_WARN_TAG("SceneSerializer", "No prefab block in file: {}", path);
				return entt::null;
			}

			return DeserializeEntityTree(scene, definition.Entities, EntityOrigin::Prefab, prefabGuid);
		}
		catch (const std::exception& exception) {
			AIM_CORE_ERROR_TAG("SceneSerializer", "LoadEntityFromFile failed: {}", exception.what());
			return entt::null;
		}
	}

} // namespace Axiom
