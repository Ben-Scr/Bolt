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
		if (!tr) return;

		m_LocalSize = scale;

		if (!b2Shape_IsValid(m_ShapeId)) return;

		Vec2 center = this->GetCenter();
		b2Polygon polygon = b2MakeOffsetBox(tr->Scale.x * scale.x * 0.5f, tr->Scale.y * scale.y * 0.5f, b2Vec2(center.x, center.y), b2Rot_identity);
		b2Shape_SetPolygon(m_ShapeId, &polygon);
		m_LastAppliedScale = tr->Scale;
		m_LastAppliedLocalSize = m_LocalSize;
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

		// Snapshot collision callbacks BEFORE DestroyShape so the recreated b2Shape (with
		// a fresh id) inherits the user's OnCollisionEnter/Exit/Hit handlers. Without
		// this, every registered collision callback would silently disappear on an
		// IsTrigger toggle — a footgun for any gameplay code that flips trigger state
		// at runtime. DestroyShape internally calls dispatcher.UnregisterShape, which
		// is why we must snapshot first; the snapshot moves the entries out, the
		// UnregisterShape that follows is a no-op for the already-emptied buckets,
		// and RestoreCallbacks re-binds them to the new shape id.
		auto& dispatcher = PhysicsSystem2D::GetMainPhysicsWorld().GetDispatcher();
		auto savedCallbacks = dispatcher.SnapshotCallbacks(m_ShapeId);
		DestroyShape(false);
		m_ShapeId = PhysicsSystem2D::GetMainPhysicsWorld().CreateShape(m_EntityHandle, scene, m_BodyId, ShapeType::Square, sensor);
		dispatcher.RestoreCallbacks(m_ShapeId, std::move(savedCallbacks));
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
		(void)scene;
		return m_LocalSize;
	}

	void BoxCollider2DComponent::SyncWithTransform(const Scene& scene) {
		const Transform2DComponent* tr = TryGetTransform(scene, m_EntityHandle, "SyncWithTransform");
		if (!tr) return;
		// Re-sync when either the entity's transform scale OR the inspector-edited
		// local size has changed — checking only Scale missed inspector tweaks to
		// m_LocalSize, leaving the b2Polygon visibly out of sync with the rendered box.
		if (tr->Scale == m_LastAppliedScale && m_LocalSize == m_LastAppliedLocalSize) return;
		if (!b2Shape_IsValid(m_ShapeId)) return;

		Vec2 center = this->GetCenter();
		b2Polygon polygon = b2MakeOffsetBox(
			tr->Scale.x * m_LocalSize.x * 0.5f,
			tr->Scale.y * m_LocalSize.y * 0.5f,
			b2Vec2(center.x, center.y),
			b2Rot_identity);
		b2Shape_SetPolygon(m_ShapeId, &polygon);
		m_LastAppliedScale = tr->Scale;
		m_LastAppliedLocalSize = m_LocalSize;
	}

	void BoxCollider2DComponent::SetCenter(const Vec2& center, const Scene& scene) {
		const Transform2DComponent* tr = TryGetTransform(scene, m_EntityHandle, "SetCenter");
		if (!tr) return;
		if (!b2Shape_IsValid(m_ShapeId)) return;

		b2Polygon polygon = b2MakeOffsetBox(
			tr->Scale.x * m_LocalSize.x * 0.5f,
			tr->Scale.y * m_LocalSize.y * 0.5f,
			b2Vec2(center.x, center.y),
			b2Rot_identity);
		b2Shape_SetPolygon(m_ShapeId, &polygon);
		m_LastAppliedScale = tr->Scale;
		m_LastAppliedLocalSize = m_LocalSize;
	}

	Vec2 BoxCollider2DComponent::GetCenter() const {
		b2Polygon polygon = b2Shape_GetPolygon(m_ShapeId);
		return Vec2(polygon.centroid.x, polygon.centroid.y);
	}

	// Idempotent: destroy hooks fire from both Rigidbody2D and this component.
	void BoxCollider2DComponent::Destroy() {
		if (b2Shape_IsValid(m_ShapeId)) {
			DestroyShape(true);
		}
		if (b2Body_IsValid(m_BodyId)) {
			if (PhysicsSystem2D::IsInitialized()) {
				PhysicsSystem2D::GetMainPhysicsWorld().UnregisterBodyBinding(m_BodyId);
			}
			b2DestroyBody(m_BodyId);
		}
		m_ShapeId = b2_nullShapeId;
		m_BodyId = b2_nullBodyId;
	}
}
