#pragma once
#include <optional>
#include "Physics/Box2DWorld.hpp"
#include "Physics/BoltPhysicsWorld2D.hpp"

namespace Bolt {

	class PhysicsSystem2D {
	public:
		void FixedUpdate(float dt);

		void Initialize();
		void Shutdown();

		static Box2DWorld& GetMainPhysicsWorld() { return s_MainWorld.value(); }
		static BoltPhysicsWorld2D& GetBoltPhysicsWorld() { return s_BoltWorld.value(); }
		static bool IsInitialized() { return s_MainWorld.has_value() && s_BoltWorld.has_value(); }
		static bool IsEnabled() { return s_IsEnabled; };
		static void SetEnabled(bool enabled) { s_IsEnabled = enabled; }
	private:
		static std::optional<Box2DWorld> s_MainWorld;
		static std::optional<BoltPhysicsWorld2D> s_BoltWorld;
		static bool s_IsEnabled;
	};
}
