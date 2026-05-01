#include <pch.hpp>
#include <Components/Physics/BoxCollider2DComponent.hpp>
#include <Components/General/Transform2DComponent.hpp>

#include <Scene/Scene.hpp>

namespace Axiom {
	namespace {
		const Transform2DComponent* TryGetTransform(const Scene& scene, EntityHandle entity, const char* context) {
			if (entity == entt::null || !scene.IsValid(entity) || !scene.HasComponent<Transform2DComponent>(entity)) {
				AIM_CORE_WARN_TAG("BoxCollider2D", "{} skipped because entity {} has no Transform2DComponent", context, static_cast<uint32_t>(entity));
				return nullptr;
			}

			return &scene.GetComponent<Transform2DComponent>(entity);
		}
	}

	void BoxCollider2DComponent::SetScale(const Vec2& scale, const Scene& scene) {
		const Transform2DComponent* tr = TryGetTransform(scene, m_EntityHandle, "SetScale");
		if (!tr) {
			return;
		}

		Vec2 center = this->GetCenter();
		b2Polygon polygon = b2MakeOffsetBox(tr->Scale.x * scale.x * 0.5f, tr->Scale.y * scale.y * 0.5f, b2Vec2(center.x, center.y), tr->GetB2Rotation());
		b2Shape_SetPolygon(m_ShapeId, &polygon);
	}

	void BoxCollider2DComponent::SetEnabled(bool enabled) {
		if (enabled)
			b2Body_Enable(m_BodyId);
		else
			b2Body_Disable(m_BodyId);
	}

	void BoxCollider2DComponent::SetSensor(bool sensor, Scene& scene) {
		if (!IsValid() || IsSensor() == sensor) {
			return;
		}

		const Vec2 localScale = GetLocalScale(scene);
		const Vec2 center = GetCenter();
		const float friction = GetFriction();
		const float bounciness = GetBounciness();
		const uint64_t layer = GetLayer();
		const bool enabled = IsEnabled();
		const bool registerContacts = CanRegisterContacts();

		DestroyShape(false);
		m_ShapeId = PhysicsSystem2D::GetMainPhysicsWorld().CreateShape(m_EntityHandle, scene, m_BodyId, ShapeType::Square, sensor);
		SetCenter(center, scene);
		SetScale(localScale, scene);
		SetFriction(friction);
		SetBounciness(bounciness);
		SetLayer(layer);
		SetRegisterContacts(registerContacts);
		SetEnabled(enabled);
	}

	Vec2 BoxCollider2DComponent::GetScale() const {
		b2ShapeType shapeType = b2Shape_GetType(m_ShapeId);

		AIM_ASSERT(shapeType == b2_polygonShape, AxiomErrorCode::Undefined, "This b2shape type isn't type of b2_polygonShape");

		b2Polygon polygon = b2Shape_GetPolygon(m_ShapeId);

		AIM_ASSERT(polygon.count == 4, AxiomErrorCode::Undefined, "b2shape polygon count equals " + std::to_string(polygon.count) + " instead of 4");

		Vec2 size = Vec2(
			polygon.vertices[2].x - polygon.vertices[0].x,
			polygon.vertices[2].y - polygon.vertices[0].y
		);
		return size;
	}

	Vec2 BoxCollider2DComponent::GetLocalScale(const Scene& scene) const {
		const Transform2DComponent* tr = TryGetTransform(scene, m_EntityHandle, "GetLocalScale");
		if (!tr) {
			return Vec2{ 1.0f, 1.0f };
		}

		b2ShapeType shapeType = b2Shape_GetType(m_ShapeId);

		AIM_ASSERT(shapeType == b2_polygonShape, AxiomErrorCode::Undefined, "This boxshape type isn't type of b2_polygonShape");

		b2Polygon polygon = b2Shape_GetPolygon(m_ShapeId);

		AIM_ASSERT(polygon.count == 4, AxiomErrorCode::Undefined, "This boxshape polygon count equals " + std::to_string(polygon.count) + " instead of 4");

		Vec2 size = Vec2(
			polygon.vertices[2].x - polygon.vertices[0].x,
			polygon.vertices[2].y - polygon.vertices[0].y
		);
		size += Vec2{ 1.f };
		return size - tr->Scale;
	}

	void BoxCollider2DComponent::UpdateScale(const Scene& scene) {
		const Transform2DComponent* tr = TryGetTransform(scene, m_EntityHandle, "UpdateScale");
		if (!tr) {
			return;
		}

		Vec2 center = this->GetCenter();

		b2Polygon polygon = b2MakeOffsetBox(tr->Scale.x * 0.5f, tr->Scale.y * 0.5f, b2Vec2(center.x, center.y), tr->GetB2Rotation());
		b2Shape_SetPolygon(m_ShapeId, &polygon);
	}

	void BoxCollider2DComponent::SetCenter(const Vec2& center, const Scene& scene) {
		const Transform2DComponent* tr = TryGetTransform(scene, m_EntityHandle, "SetCenter");
		if (!tr) {
			return;
		}

		b2Polygon polygon = b2MakeOffsetBox(tr->Scale.x * 0.5f, tr->Scale.y * 0.5f, b2Vec2(center.x, center.y), b2Rot_identity);
		b2Shape_SetPolygon(m_ShapeId, &polygon);
	}

	Vec2 BoxCollider2DComponent::GetCenter() const {
		b2Polygon polygon = b2Shape_GetPolygon(m_ShapeId);
		return Vec2(polygon.centroid.x, polygon.centroid.y);
	}
}
