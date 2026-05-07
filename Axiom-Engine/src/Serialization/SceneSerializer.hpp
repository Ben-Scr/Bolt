#pragma once
#include "Components/General/EntityMetaDataComponent.hpp"
#include "Core/Export.hpp"
#include "Scene/EntityHandle.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace Axiom {

	class Scene;

	class Entity;

	namespace Json {
		class Value;
	}

	class AXIOM_API SceneSerializer {
	public:
		static Json::Value SerializeScene(Scene& scene);
		static bool DeserializeScene(Scene& scene, const Json::Value& root, std::string_view source = {});

		static bool SaveToFile(Scene& scene, const std::string& path);
		static bool LoadFromFile(Scene& scene, const std::string& path);

		// Prefab support: save/load single entities
		static Json::Value SerializeEntityFull(Scene& scene, EntityHandle entity);
		static Json::Value SerializeEntityForClipboard(Scene& scene, EntityHandle entity);
		static EntityHandle DeserializeEntityFromValue(Scene& scene, const Json::Value& entityValue);
		static Json::Value SerializeComponent(Scene& scene, EntityHandle entity, std::string_view componentName);
		static bool DeserializeComponent(Scene& scene, EntityHandle entity, std::string_view componentName, const Json::Value& componentValue);
		static bool ResetComponent(Scene& scene, EntityHandle entity, std::string_view componentName);
		static bool SaveEntityToFile(Scene& scene, EntityHandle entity, const std::string& path);
		static EntityHandle LoadEntityFromFile(Scene& scene, const std::string& path);
		static EntityHandle InstantiatePrefab(Scene& scene, uint64_t prefabGuid);
		static bool ApplyPrefabInstanceOverrides(Scene& scene, EntityHandle entity);
		static EntityHandle RevertPrefabInstanceOverride(Scene& scene, EntityHandle entity, const std::string& overridePath);

		// Re-instantiate a prefab instance against the current on-disk source
		// while preserving the instance's per-field overrides. `previousSourceEntityValue`
		// is the prefab's "Entity" JSON block as it existed BEFORE the source was
		// saved — it's the baseline against which overrides are computed and then
		// re-applied on top of the new source. Returns the new entity handle
		// (the old one is destroyed). Used by the prefab inspector to push edits
		// to live instances without dropping their per-field overrides.
		static EntityHandle RefreshPrefabInstance(Scene& scene, EntityHandle existing,
			const Json::Value& previousSourceEntityValue);

		// Compute the per-field override set for a single prefab instance:
		// outOverrides is filled with `{ "Transform2D.posX": <value>, ... }`
		// — keys are dot-paths into the entity's serialized form. Returns true
		// only when the entity is an `EntityOrigin::Prefab` instance whose source
		// GUID resolves in the AssetRegistry; orphans and non-instances return
		// false with `outOverrides` left empty. Used by the inspector to flag
		// overridden components/fields and feed the Revert Field UI.
		static bool ComputeInstanceOverrides(Scene& scene, EntityHandle entity, Json::Value& outOverrides);

	private:
		static EntityHandle DeserializeEntity(Scene& scene, const Json::Value& entityValue);
		static EntityHandle DeserializeFullEntity(
			Scene& scene,
			const Json::Value& entityValue,
			EntityOrigin origin,
			uint64_t prefabGuid = 0);
		static EntityHandle DeserializeEntityTree(
			Scene& scene,
			const std::vector<Json::Value>& entityValues,
			EntityOrigin origin,
			uint64_t prefabGuid = 0,
			bool preserveSerializedIdentity = true);
		static EntityHandle DeserializePrefabInstance(Scene& scene, const Json::Value& entityValue);
	};

} // namespace Axiom
