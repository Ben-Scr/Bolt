#pragma once
#include "Components/General/EntityMetaDataComponent.hpp"
#include "Core/Export.hpp"
#include "Scene/EntityHandle.hpp"
#include <string>
#include <string_view>

namespace Bolt {

	class Scene;

	class Entity;

	namespace Json {
		class Value;
	}

	class BOLT_API SceneSerializer {
	public:
		static Json::Value SerializeScene(Scene& scene);
		static bool DeserializeScene(Scene& scene, const Json::Value& root, std::string_view source = {});

		static bool SaveToFile(Scene& scene, const std::string& path);
		static bool LoadFromFile(Scene& scene, const std::string& path);

		// Prefab support: save/load single entities
		static Json::Value SerializeEntityFull(Scene& scene, EntityHandle entity);
		static bool SaveEntityToFile(Scene& scene, EntityHandle entity, const std::string& path);
		static EntityHandle LoadEntityFromFile(Scene& scene, const std::string& path);
		static EntityHandle InstantiatePrefab(Scene& scene, uint64_t prefabGuid);
		static bool ApplyPrefabInstanceOverrides(Scene& scene, EntityHandle entity);
		static EntityHandle RevertPrefabInstanceOverride(Scene& scene, EntityHandle entity, const std::string& overridePath);

	private:
		static EntityHandle DeserializeEntity(Scene& scene, const Json::Value& entityValue);
		static EntityHandle DeserializeFullEntity(
			Scene& scene,
			const Json::Value& entityValue,
			EntityOrigin origin,
			uint64_t prefabGuid = 0);
		static EntityHandle DeserializePrefabInstance(Scene& scene, const Json::Value& entityValue);
	};

} // namespace Bolt
