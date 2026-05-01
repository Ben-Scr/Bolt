#pragma once
#include <optional>
#include "Physics/Box2DWorld.hpp"
#include "Physics/AxiomPhysicsWorld2D.hpp"

namespace Axiom {

	class PhysicsSystem2D {
	public:
		void FixedUpdate(float dt);

		void Initialize();
		void Shutdown();

		static Box2DWorld& GetMainPhysicsWorld() { return s_MainWorld.value(); }
		static AxiomPhysicsWorld2D& GetAxiomPhysicsWorld() { return s_AxiomWorld.value(); }
		static bool IsInitialized() { return s_MainWorld.has_value() && s_AxiomWorld.has_value(); }
		static bool IsEnabled() { return s_IsEnabled; };
		static void SetEnabled(bool enabled) { s_IsEnabled = enabled; }
	private:
		static std::optional<Box2DWorld> s_MainWorld;
		static std::optional<AxiomPhysicsWorld2D> s_AxiomWorld;
		static bool s_IsEnabled;
	};
}
