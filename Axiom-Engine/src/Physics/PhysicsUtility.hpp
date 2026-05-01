#pragma once
#include <Scene/EntityHandle.hpp>

namespace Axiom {
	class Collider2D;
	class Rigidbody2DComponent;
}

struct b2BodyId;
struct b2ShapeId;

namespace Axiom {
	class PhysicsUtility {
	public:
		static EntityHandle GetEntityHandleFromCollider(const Collider2D& collider);
		static EntityHandle GetEntityHandleFromRigidbody(const Rigidbody2DComponent& rb);
		static EntityHandle GetEntityHandleFromBodyId(b2BodyId bodyId);
		static EntityHandle GetEntityHandleFromShapeID(b2ShapeId shapeId);
	};
}