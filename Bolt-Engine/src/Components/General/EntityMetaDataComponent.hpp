#pragma once

#include "Collections/Ids.hpp"

namespace Bolt {

	enum class EntityOrigin {
		Scene = 0,
		Prefab,
		Runtime
	};

	struct EntityMetaData {
		EntityID RuntimeID = 0;
		AssetGUID PrefabGUID = AssetGUID(0);
		AssetGUID SceneGUID = AssetGUID(0);
		EntityOrigin Origin = EntityOrigin::Runtime;
	};

	struct EntityMetaDataComponent {
		EntityMetaData MetaData;
	};

}
