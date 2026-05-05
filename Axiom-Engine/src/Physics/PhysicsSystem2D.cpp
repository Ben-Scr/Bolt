#include "pch.hpp"

#include <Components/Physics/Rigidbody2DComponent.hpp>
#include <Components/Physics/FastBody2DComponent.hpp>
#include <Components/Physics/BoxCollider2DComponent.hpp>
#include <Components/General/Transform2DComponent.hpp>
#include <Components/Tags.hpp>

#include "Physics/PhysicsSystem2D.hpp"
#include "Physics/Box2DWorld.hpp"
#include "Physics/Collision2D.hpp"

#include "Profiling/Profiler.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/Scene.hpp"
#include "Scripting/ScriptSystem.hpp"


namespace Axiom {
	bool PhysicsSystem2D::s_IsEnabled = true;
	std::optional<Box2DWorld> PhysicsSystem2D::s_MainWorld;
	std::optional<AxiomPhysicsWorld2D> PhysicsSystem2D::s_AxiomWorld;

	void PhysicsSystem2D::Initialize() {
		s_MainWorld.emplace();
		s_AxiomWorld.emplace();
		AIM_CORE_INFO_TAG("PhysicsSystem", "Box2D + Axiom-Physics initialized");
	}

	void PhysicsSystem2D::SyncTransformsToPhysics() {
		// Pre-step sync. BoxCollider polygon FIRST so its dirty-read
		// happens before the body loops clear the dirty flag.
		SceneManager::Get().ForeachLoadedScene([](Scene& scene) {
			auto& registry = scene.GetRegistry();

			for (auto [ent, box, tf] : registry.view<BoxCollider2DComponent, Transform2DComponent>(entt::exclude<DisabledTag>).each()) {
				if (tf.IsDirty() && box.IsValid()) {
					box.SyncWithTransform(scene);
				}
			}

			for (auto [ent, rb, tf] : registry.view<Rigidbody2DComponent, Transform2DComponent>(entt::exclude<DisabledTag>).each()) {
				if (tf.IsDirty() && rb.IsValid()) {
					rb.SetTransform(tf);
					tf.ClearDirty();
				}
			}

			for (auto [ent, body, tf] : registry.view<FastBody2DComponent, Transform2DComponent>(entt::exclude<DisabledTag>).each()) {
				if (tf.IsDirty() && body.m_Body) {
					body.SetPosition(tf.Position);
					tf.ClearDirty();
				}
			}

			// Clear dirty for collider-only entities (no body sync touched them).
			for (auto [ent, box, tf] : registry.view<BoxCollider2DComponent, Transform2DComponent>(entt::exclude<DisabledTag, Rigidbody2DComponent, FastBody2DComponent>).each()) {
				if (tf.IsDirty()) {
					tf.ClearDirty();
				}
			}
		});
	}

	void PhysicsSystem2D::Update() {
		if (!s_IsEnabled) return;
		SyncTransformsToPhysics();
	}

	/* static */ void PhysicsSystem2D::WakeAllBodies() {
		// AxiomPhys (FastBody2D) has no sleep optimization, so only Box2D
		// rigidbodies need their sleep timer reset.
		SceneManager::Get().ForeachLoadedScene([](Scene& scene) {
			auto& registry = scene.GetRegistry();
			for (auto [ent, rb] : registry.view<Rigidbody2DComponent>(entt::exclude<DisabledTag>).each()) {
				if (rb.IsValid()) {
					b2Body_SetAwake(rb.GetBodyHandle(), true);
				}
			}
		});
	}

	void PhysicsSystem2D::FixedUpdate(float dt) {
		AXIOM_PROFILE_SCOPE("Physics");
		if (!s_IsEnabled) return;
		if (!s_MainWorld || !s_AxiomWorld) return;

		SyncTransformsToPhysics();

		// Step both worlds, then sync poses back, then dispatch callbacks
		// — so callbacks see fully-settled scene state.
		s_MainWorld->Step(dt);
		s_AxiomWorld->Step(dt);

		SceneManager::Get().ForeachLoadedScene([](Scene& scene) {
			auto& registry = scene.GetRegistry();

			for (auto [ent, rb, tf] : registry.view<Rigidbody2DComponent, Transform2DComponent>(entt::exclude<DisabledTag>).each()) {
				if (!rb.IsValid()) continue;
				tf.Position = rb.GetPosition();
				tf.Rotation = rb.GetRotation();
				tf.ClearDirty();
			}

			for (auto [ent, body, tf] : registry.view<FastBody2DComponent, Transform2DComponent>(entt::exclude<DisabledTag>).each()) {
				if (body.m_Body) {
					auto pos = body.m_Body->GetPosition();
					tf.Position = { pos.x, pos.y };
					tf.ClearDirty();
				}
			}
		});

		s_MainWorld->GetDispatcher().Process(
			s_MainWorld->GetWorldID(),
			[](const Collision2D& collision) {
				SceneManager::Get().ForeachLoadedScene([&](Scene& scene) {
					if (scene.IsValid(collision.entityA) && scene.IsValid(collision.entityB)) {
						ScriptSystem::DispatchCollisionEnter2D(scene, collision);
					}
				});
			},
			[](const Collision2D& collision) {
				SceneManager::Get().ForeachLoadedScene([&](Scene& scene) {
					if (scene.IsValid(collision.entityA) && scene.IsValid(collision.entityB)) {
						ScriptSystem::DispatchCollisionExit2D(scene, collision);
					}
				});
			},
			[](const Collision2D& collision) {
				SceneManager::Get().ForeachLoadedScene([&](Scene& scene) {
					if (scene.IsValid(collision.entityA) && scene.IsValid(collision.entityB)) {
						ScriptSystem::DispatchCollisionStay2D(scene, collision);
					}
				});
			});
	}

	void PhysicsSystem2D::Shutdown() {
		if (s_AxiomWorld) {
			s_AxiomWorld->Destroy();
			s_AxiomWorld.reset();
		}
		s_MainWorld.reset();
	}
}
