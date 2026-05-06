#pragma once
#include "Scene/EntityHandle.hpp"

#include <vector>

namespace Axiom {

	// Parent-child relationship between entities. The component is added on
	// demand: a freshly-created entity has no HierarchyComponent and is
	// implicitly a root with no children. Adding a child promotes both the
	// parent and the child to having a HierarchyComponent.
	//
	// Invariants (maintained by Entity::SetParent / Scene::DestroyEntity):
	//   - If entity X lists Y in Children, Y has a HierarchyComponent with
	//     Parent == X.
	//   - The Children vector contains no duplicates and no entt::null.
	//   - The graph is a forest: no cycles, no shared children. Re-parenting
	//     unlinks from the previous parent first.
	//
	// Iteration tips:
	//   - For a top-down walk (parent before children) recurse via Children.
	//   - To enumerate roots in a scene, iterate every entity and skip those
	//     where HierarchyComponent::Parent != entt::null. Entities without
	//     the component are also roots.
	struct HierarchyComponent {
		EntityHandle Parent = entt::null;
		std::vector<EntityHandle> Children;
	};

}
