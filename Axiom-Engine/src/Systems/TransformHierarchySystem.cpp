#include "pch.hpp"
#include "Systems/TransformHierarchySystem.hpp"
#include "Scene/Scene.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/Physics/FastBody2DComponent.hpp"

namespace Axiom {
	namespace {
		// Hard cap recursion depth so a corrupted hierarchy can't infinite-loop.
		constexpr int kMaxHierarchyDepth = 1024;

		// Compose `child`'s authored Local* against `parent`'s world transform.
		// Box2D and the Axiom-Physics body components own their entity's world
		// position/rotation, so we leave their fields untouched — but we still
		// recurse into their children with the body-driven world transform as
		// the parent so the visual subtree follows the body.
		void ProcessSubtree(entt::registry& registry, EntityHandle entity,
			const Transform2DComponent* parentWorld, int depth)
		{
			if (depth > kMaxHierarchyDepth) return;
			if (!registry.valid(entity)) return;

			Transform2DComponent* tr = registry.try_get<Transform2DComponent>(entity);
			if (!tr) return;

			const bool ownedByPhysics = registry.any_of<Rigidbody2DComponent, FastBody2DComponent>(entity);
			if (!ownedByPhysics) {
				if (parentWorld) {
					tr->Position = parentWorld->TransformPoint(tr->LocalPosition);
					tr->Rotation = parentWorld->Rotation + tr->LocalRotation;
					tr->Scale = { parentWorld->Scale.x * tr->LocalScale.x,
								  parentWorld->Scale.y * tr->LocalScale.y };
				}
				else {
					tr->Position = tr->LocalPosition;
					tr->Rotation = tr->LocalRotation;
					tr->Scale = tr->LocalScale;
				}
			}

			if (registry.all_of<HierarchyComponent>(entity)) {
				// Snapshot the children list so structural edits during the
				// pass (rare, but possible if a script mutates) don't iterate
				// over invalidated storage.
				const auto childrenSnapshot = registry.get<HierarchyComponent>(entity).Children;
				for (EntityHandle child : childrenSnapshot) {
					ProcessSubtree(registry, child, tr, depth + 1);
				}
			}
		}

		void RunPropagation(Scene& scene) {
			auto& registry = scene.GetRegistry();
			auto view = registry.view<Transform2DComponent>();
			for (auto entity : view) {
				bool isRoot = true;
				if (registry.all_of<HierarchyComponent>(entity)) {
					isRoot = registry.get<HierarchyComponent>(entity).Parent == entt::null;
				}
				if (!isRoot) continue;
				ProcessSubtree(registry, entity, nullptr, 0);
			}
		}
	}

	void TransformHierarchySystem::Awake(Scene& scene) {
		// One-shot at scene awake so post-load world values are valid for
		// scripts' Awake/Start hooks.
		RunPropagation(scene);
	}

	void TransformHierarchySystem::Update(Scene& scene) {
		RunPropagation(scene);
	}

	void TransformHierarchySystem::OnPreRender(Scene& scene) {
		// Pre-render runs in both edit and play modes (Update is gated by play
		// mode), so this is the hook that keeps children visually following a
		// parent dragged in the inspector while the scene isn't ticking.
		RunPropagation(scene);
	}

	void TransformHierarchySystem::Propagate(Scene& scene) {
		RunPropagation(scene);
	}
}
