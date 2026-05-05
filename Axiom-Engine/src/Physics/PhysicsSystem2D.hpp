#pragma once
#include <optional>
#include "Core/Export.hpp"
#include "Physics/Box2DWorld.hpp"
#include "Physics/AxiomPhysicsWorld2D.hpp"

namespace Axiom {

	class AXIOM_API PhysicsSystem2D {
	public:
		void FixedUpdate(float dt);
		// Editor-mode + play-mode sync. No simulation step.
		void Update();

		// Reset Box2D's sleep timer on every Rigidbody2D body across all
		// loaded scenes. Call when entering play mode so bodies that sat
		// in editor mode aren't immediately considered idle.
		static void WakeAllBodies();

		void Initialize();
		void Shutdown();

		static Box2DWorld& GetMainPhysicsWorld() { return s_MainWorld.value(); }
		static AxiomPhysicsWorld2D& GetAxiomPhysicsWorld() { return s_AxiomWorld.value(); }
		static bool IsInitialized() { return s_MainWorld.has_value() && s_AxiomWorld.has_value(); }
		static bool IsEnabled() { return s_IsEnabled; };
		static void SetEnabled(bool enabled) { s_IsEnabled = enabled; }
	private:
		void SyncTransformsToPhysics();

		static std::optional<Box2DWorld> s_MainWorld;
		static std::optional<AxiomPhysicsWorld2D> s_AxiomWorld;
		static bool s_IsEnabled;
	};
}
