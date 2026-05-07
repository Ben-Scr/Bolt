#include "pch.hpp"

#include <Components/Physics/Rigidbody2DComponent.hpp>
#include <Components/Physics/FastBody2DComponent.hpp>
#include <Components/Physics/BoxCollider2DComponent.hpp>
#include <Components/General/Transform2DComponent.hpp>
#include <Components/Tags.hpp>

#include "Physics/PhysicsSystem2D.hpp"
#include "Physics/Box2DWorld.hpp"
#include "Physics/Collision2D.hpp"

#include "Core/Application.hpp"
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
		// FixedUpdate already runs the same per-scene sync during play mode, so
		// repeating it from Update doubles the dirty-flag walk every frame for
		// no gain. Editor mode still needs Update to drive the sync because
		// FixedUpdate is gated off.
		if (Application::GetIsPlaying() && !Application::IsPaused()) {
			return;
		}
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

		auto dispatchToCollisionScenes = [](const Collision2D& collision, const auto& dispatch) {
			if (collision.sceneA && collision.sceneA->IsValid(collision.entityA)) {
				dispatch(*collision.sceneA, collision);
			}
			if (collision.sceneB && collision.sceneB != collision.sceneA && collision.sceneB->IsValid(collision.entityB)) {
				dispatch(*collision.sceneB, collision);
			}
		};

		s_MainWorld->GetDispatcher().Process(
			s_MainWorld->GetWorldID(),
			[world = &*s_MainWorld](b2ShapeId shapeId) { return world->ResolveShape(shapeId); },
			[&dispatchToCollisionScenes](const Collision2D& collision) {
				dispatchToCollisionScenes(collision, [](Scene& scene, const Collision2D& c) {
					ScriptSystem::DispatchCollisionEnter2D(scene, c);
				});
			},
			[&dispatchToCollisionScenes](const Collision2D& collision) {
				dispatchToCollisionScenes(collision, [](Scene& scene, const Collision2D& c) {
					ScriptSystem::DispatchCollisionExit2D(scene, c);
				});
			},
			[&dispatchToCollisionScenes](const Collision2D& collision) {
				dispatchToCollisionScenes(collision, [](Scene& scene, const Collision2D& c) {
					ScriptSystem::DispatchCollisionStay2D(scene, c);
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
