#pragma once
#include "Physics/CollisionDispatcher.hpp"
#include "Scene/EntityHandle.hpp"
#include "Physics/PhysicsTypes.hpp"
#include <box2d/box2d.h>

namespace Axiom {
    class Scene;
}

namespace Axiom {

    class Box2DWorld {
        friend class Physics2D;

    public:
		Box2DWorld();
		~Box2DWorld();

		Box2DWorld(const Box2DWorld&) = delete;
		Box2DWorld& operator=(const Box2DWorld&) = delete;

		Box2DWorld(Box2DWorld&& other) noexcept;
		Box2DWorld& operator=(Box2DWorld&& other) noexcept;

        void Step(float dt);


        b2BodyId CreateBody(EntityHandle nativeEntity, Scene& scene, BodyType bodyType);
        b2ShapeId CreateShape(EntityHandle nativeEntity, Scene& scene, b2BodyId bodyId, ShapeType shapeType, bool isSensor = false);

        CollisionDispatcher& GetDispatcher();
        b2WorldId GetWorldID() { return m_WorldId; }
        void Destroy();
    private:
        b2WorldId m_WorldId = b2_nullWorldId;
        CollisionDispatcher m_Dispatcher{};
    };
}
