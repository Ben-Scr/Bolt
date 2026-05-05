#pragma once
#include <cstdint>

namespace Axiom {

	// Drag-and-drop payload for the "HIERARCHY_ENTITY" payload type.
	// Used when dragging entities within the Hierarchy panel (reorder)
	// or onto external drop targets such as the Asset Browser
	// (turn an entity into a prefab) or property inspectors
	// (assign an entity reference). Layout MUST stay stable so all
	// producer/consumer call sites stay binary-compatible.
	struct HierarchyDragData {
		int Index;
		uint32_t EntityHandle;
	};

}
