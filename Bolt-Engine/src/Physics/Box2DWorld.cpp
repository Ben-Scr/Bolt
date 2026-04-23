#include "pch.hpp"
#include "Box2DWorld.hpp"
#include "Scene/Scene.hpp"
#include <Components/General/Transform2DComponent.hpp>

namespace Bolt {
	Box2DWorld::Box2DWorld() {
		b2WorldDef def = b2DefaultWorldDef();
		def.enableSleep = true;

		def.gravity = b2Vec2{ 0, -9.8f };
		m_WorldId = b2CreateWorld(&def);
	}
	Box2DWorld::~Box2DWorld() {
		Destroy();
	}

	Box2DWorld::Box2DWorld(Box2DWorld&& other) noexcept
		: m_WorldId(other.m_WorldId), m_Dispatcher(std::move(other.m_Dispatcher)) {
		other.m_WorldId = b2_nullWorldId;
	}

	Box2DWorld& Box2DWorld::operator=(Box2DWorld&& other) noexcept {
		if (this == &other) {
			return *this;
		}

		Destroy();
		m_WorldId = other.m_WorldId;
		m_Dispatcher = std::move(other.m_Dispatcher);
		other.m_WorldId = b2_nullWorldId;
		return *this;
	}

	void Box2DWorld::Step(float dt) {
		b2World_Step(m_WorldId, dt, 5);
	}

	void Box2DWorld::Destroy() {
		if (b2World_IsValid(m_WorldId)) {
			b2DestroyWorld(m_WorldId);
			m_WorldId = b2_nullWorldId;
		}
		m_Dispatcher.Clear();
	}

	b2BodyId Box2DWorld::CreateBody(EntityHandle nativeEntity, Scene& scene, BodyType bodyType) {
		Transform2DComponent defaultTransform{};
		Transform2DComponent* tr = &defaultTransform;
		if (!scene.TryGetComponent(nativeEntity, tr)) {
			BT_CORE_WARN_TAG("PhysicsSystem", "CreateBody using default transform because entity {} has no Transform2DComponent", static_cast<uint32_t>(nativeEntity));
			tr = &defaultTransform;
		}

		b2Vec2 box2dPos(tr->Position.x, tr->Position.y);
		b2BodyDef bodyDef = b2DefaultBodyDef();
		bodyDef.type = bodyType == BodyType::Dynamic ? b2_dynamicBody : (bodyType == BodyType::Static ? b2_staticBody : b2_kinematicBody);
		bodyDef.gravityScale = 1.0f;
		bodyDef.position = box2dPos;
		bodyDef.rotation = tr->GetB2Rotation();
		bodyDef.isBullet = false;
		bodyDef.userData = reinterpret_cast<void*>(static_cast<uintptr_t>(nativeEntity));
		bodyDef.linearDamping = 0.1f;

		b2BodyId bodyId = b2CreateBody(m_WorldId, &bodyDef);
		return bodyId;
	}

	b2ShapeId Box2DWorld::CreateShape(EntityHandle nativeEntity, Scene& scene, b2BodyId bodyId, ShapeType shapeType, bool isSensor) {
		Transform2DComponent transform{};
		Transform2DComponent* transformPtr = &transform;
		if (!scene.TryGetComponent(nativeEntity, transformPtr)) {
			BT_CORE_WARN_TAG("PhysicsSystem", "CreateShape using default transform because entity {} has no Transform2DComponent", static_cast<uint32_t>(nativeEntity));
			transform = Transform2DComponent{};
			transformPtr = &transform;
		}

		transform = *transformPtr;
		b2ShapeId shapeId = b2_nullShapeId;

		b2ShapeDef shapeDef = b2DefaultShapeDef();
		shapeDef.density = 1.f;
		shapeDef.material.friction = 0.3f;
		shapeDef.material.restitution = 0.f;
		shapeDef.isSensor = isSensor;
		shapeDef.enableSensorEvents = isSensor;

		if (shapeType == ShapeType::Square) {

			b2Polygon b2Polygon = b2MakeBox(0.5f * transform.Scale.x, 0.5f * transform.Scale.y);
			shapeId = b2CreatePolygonShape(bodyId, &shapeDef, &b2Polygon);
		}
		else if (shapeType == ShapeType::Circle)
		{
			float r = 0.25f * (transform.Scale.x + transform.Scale.y);
			b2Circle circle = { b2Vec2{0,0}, r };
			shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);
		}


		b2Body_SetTransform(bodyId, b2Vec2(transform.Position.x, transform.Position.y), transform.GetB2Rotation());
		return shapeId;
	}

	CollisionDispatcher& Box2DWorld::GetDispatcher() { return m_Dispatcher; }
}
