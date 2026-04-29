#include "pch.hpp"

#include <Components/Physics/Rigidbody2DComponent.hpp>
#include <Components/Physics/FastBody2DComponent.hpp>
#include <Components/General/Transform2DComponent.hpp>
#include <Components/Tags.hpp>

#include "Physics/PhysicsSystem2D.hpp"
#include "Physics/Box2DWorld.hpp"
#include "Physics/Collision2D.hpp"

#include "Scene/SceneManager.hpp"
#include "Scene/Scene.hpp"
#include "Scripting/ScriptSystem.hpp"


namespace Bolt {
	bool PhysicsSystem2D::s_IsEnabled = true;
	std::optional<Box2DWorld> PhysicsSystem2D::s_MainWorld;
	std::optional<BoltPhysicsWorld2D> PhysicsSystem2D::s_BoltWorld;

	void PhysicsSystem2D::Initialize() {
		s_MainWorld.emplace();
		s_BoltWorld.emplace();
		BT_CORE_INFO_TAG("PhysicsSystem", "Box2D + Bolt-Physics initialized");
	}

	void PhysicsSystem2D::FixedUpdate(float dt) {
		if (!s_IsEnabled) return;

		// Box2D simulation
		s_MainWorld->Step(dt);
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
			});

		// Box2D transform sync
		SceneManager::Get().ForeachLoadedScene([](Scene& scene) {
			for (auto [ent, rb, tf] : scene.GetRegistry().view<Rigidbody2DComponent, Transform2DComponent>(entt::exclude<DisabledTag>).each()) {
				tf.Position = rb.GetPosition();
				tf.Rotation = rb.GetRotation();
			}
		});

		// Bolt-Physics simulation
		s_BoltWorld->Step(dt);

		// Bolt-Physics transform sync
		SceneManager::Get().ForeachLoadedScene([](Scene& scene) {
			for (auto [ent, body, tf] : scene.GetRegistry().view<FastBody2DComponent, Transform2DComponent>(entt::exclude<DisabledTag>).each()) {
				if (body.m_Body) {
					auto pos = body.m_Body->GetPosition();
					tf.Position = { pos.x, pos.y };
				}
			}
		});
	}

	void PhysicsSystem2D::Shutdown() {
		if (s_BoltWorld) {
			s_BoltWorld->Destroy();
			s_BoltWorld.reset();
		}
		s_MainWorld.reset();
	}
}
