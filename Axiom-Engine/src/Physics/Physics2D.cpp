#include "pch.hpp"

#include "Physics/Physics2D.hpp"
#include "Physics/PhysicsSystem2D.hpp"
#include "Physics/Box2DWorld.hpp"
#include "Scene/Scene.hpp"

#include <Collections/Mat2.hpp>
#include <Math/Common.hpp>

#include <box2d/box2d.h>
#include <box2d/types.h>
#include <box2d/collision.h>

namespace Axiom {
	namespace {
		bool IsValidRef(const PhysicsBodyRef2D& ref) {
			return ref.scene && ref.entity != entt::null && ref.scene->IsValid(ref.entity);
		}

		PhysicsBodyRef2D ResolveBodyRefFromShape(Box2DWorld& phys, b2ShapeId shapeId) {
			const CollisionBodyRef body = phys.ResolveShape(shapeId);
			PhysicsBodyRef2D ref{ body.Entity, body.OwningScene };
			return IsValidRef(ref) ? ref : PhysicsBodyRef2D{};
		}

		std::optional<EntityHandle> ToEntityHandle(std::optional<PhysicsBodyRef2D> ref) {
			if (!ref.has_value() || !IsValidRef(*ref)) {
				return std::nullopt;
			}
			return ref->entity;
		}

		std::vector<EntityHandle> ToEntityHandles(const std::vector<PhysicsBodyRef2D>& refs) {
			std::vector<EntityHandle> handles;
			handles.reserve(refs.size());
			for (const PhysicsBodyRef2D& ref : refs) {
				if (IsValidRef(ref)) {
					handles.push_back(ref.entity);
				}
			}
			return handles;
		}

		std::optional<PhysicsBodyRef2D> QueryProxyRef(const Vec2& center, const b2ShapeProxy& proxy, OverlapMode mode) {
			if (!PhysicsSystem2D::IsInitialized() || !PhysicsSystem2D::IsEnabled()) {
				return std::nullopt;
			}

			auto& phys = PhysicsSystem2D::GetMainPhysicsWorld();
			b2WorldId world = phys.GetWorldID();
			b2QueryFilter filter = b2DefaultQueryFilter();

			struct QueryContext {
				Vec2 center;
				OverlapMode mode;
				Box2DWorld* phys = nullptr;
				std::optional<PhysicsBodyRef2D> first;
				std::optional<PhysicsBodyRef2D> nearest;
				float bestDist2 = std::numeric_limits<float>::max();

				static bool Report(b2ShapeId shapeId, void* ctx) {
					auto* self = static_cast<QueryContext*>(ctx);
					PhysicsBodyRef2D ref = ResolveBodyRefFromShape(*self->phys, shapeId);
					if (!IsValidRef(ref)) {
						return true;
					}

					if (self->mode == OverlapMode::First) {
						self->first = ref;
						return false;
					}

					b2BodyId bodyId = b2Shape_GetBody(shapeId);
					b2Transform xf = b2Body_GetTransform(bodyId);
					float dx = xf.p.x - self->center.x;
					float dy = xf.p.y - self->center.y;
					float d2 = dx * dx + dy * dy;
					if (d2 < self->bestDist2) {
						self->bestDist2 = d2;
						self->nearest = ref;
					}
					return true;
				}
			} context{ center, mode, &phys, std::nullopt, std::nullopt };

			b2World_OverlapShape(world, &proxy, filter, QueryContext::Report, &context);
			return mode == OverlapMode::First ? context.first : context.nearest;
		}

		std::vector<PhysicsBodyRef2D> QueryProxyAllRefs(const b2ShapeProxy& proxy) {
			std::vector<PhysicsBodyRef2D> results;
			if (!PhysicsSystem2D::IsInitialized() || !PhysicsSystem2D::IsEnabled()) {
				return results;
			}

			auto& phys = PhysicsSystem2D::GetMainPhysicsWorld();
			b2WorldId world = phys.GetWorldID();
			b2QueryFilter filter = b2DefaultQueryFilter();

			struct QueryContext {
				Box2DWorld* phys = nullptr;
				std::vector<PhysicsBodyRef2D>* out;
				static bool Report(b2ShapeId shapeId, void* ctx) {
					auto* self = static_cast<QueryContext*>(ctx);
					PhysicsBodyRef2D ref = ResolveBodyRefFromShape(*self->phys, shapeId);
					if (IsValidRef(ref)) {
						self->out->push_back(ref);
					}
					return true;
				}
			} context{ &phys, &results };

			b2World_OverlapShape(world, &proxy, filter, QueryContext::Report, &context);
			return results;
		}

		std::optional<PhysicsBodyRef2D> QueryPointRef(const Vec2& point, OverlapMode mode) {
			if (!PhysicsSystem2D::IsInitialized() || !PhysicsSystem2D::IsEnabled()) {
				return std::nullopt;
			}

			auto& phys = PhysicsSystem2D::GetMainPhysicsWorld();
			b2WorldId world = phys.GetWorldID();
			b2QueryFilter filter = b2DefaultQueryFilter();
			constexpr float epsilon = 0.001f;
			b2AABB aabb{
				.lowerBound = { point.x - epsilon, point.y - epsilon },
				.upperBound = { point.x + epsilon, point.y + epsilon }
			};

			struct QueryContext {
				Vec2 point;
				OverlapMode mode;
				Box2DWorld* phys = nullptr;
				std::optional<PhysicsBodyRef2D> first;
				std::optional<PhysicsBodyRef2D> nearest;
				float bestDist2 = std::numeric_limits<float>::max();

				static bool Report(b2ShapeId shapeId, void* ctx) {
					auto* self = static_cast<QueryContext*>(ctx);
					if (!b2Shape_TestPoint(shapeId, { self->point.x, self->point.y })) {
						return true;
					}

					PhysicsBodyRef2D ref = ResolveBodyRefFromShape(*self->phys, shapeId);
					if (!IsValidRef(ref)) {
						return true;
					}
					if (self->mode == OverlapMode::First) {
						self->first = ref;
						return false;
					}

					b2BodyId bodyId = b2Shape_GetBody(shapeId);
					b2Transform xf = b2Body_GetTransform(bodyId);
					float dx = xf.p.x - self->point.x;
					float dy = xf.p.y - self->point.y;
					float d2 = dx * dx + dy * dy;
					if (d2 < self->bestDist2) {
						self->bestDist2 = d2;
						self->nearest = ref;
					}
					return true;
				}
			} context{ point, mode, &phys, std::nullopt, std::nullopt };

			b2World_OverlapAABB(world, aabb, filter, QueryContext::Report, &context);
			return mode == OverlapMode::First ? context.first : context.nearest;
		}

		std::vector<PhysicsBodyRef2D> QueryPointAllRefs(const Vec2& point) {
			std::vector<PhysicsBodyRef2D> results;
			if (!PhysicsSystem2D::IsInitialized() || !PhysicsSystem2D::IsEnabled()) {
				return results;
			}

			auto& phys = PhysicsSystem2D::GetMainPhysicsWorld();
			b2WorldId world = phys.GetWorldID();
			b2QueryFilter filter = b2DefaultQueryFilter();
			constexpr float epsilon = 0.001f;
			b2AABB aabb{
				.lowerBound = { point.x - epsilon, point.y - epsilon },
				.upperBound = { point.x + epsilon, point.y + epsilon }
			};

			struct QueryContext {
				Vec2 point;
				Box2DWorld* phys = nullptr;
				std::vector<PhysicsBodyRef2D>* out;

				static bool Report(b2ShapeId shapeId, void* ctx) {
					auto* self = static_cast<QueryContext*>(ctx);
					if (b2Shape_TestPoint(shapeId, { self->point.x, self->point.y })) {
						PhysicsBodyRef2D ref = ResolveBodyRefFromShape(*self->phys, shapeId);
						if (IsValidRef(ref)) {
							self->out->push_back(ref);
						}
					}
					return true;
				}
			} context{ point, &phys, &results };

			b2World_OverlapAABB(world, aabb, filter, QueryContext::Report, &context);
			return results;
		}
	}

	// TODO: assert main thread when Application::IsMainThread() lands.
	// Box2D queries below touch the live world while FixedUpdate may be
	// stepping it on another thread; calling these from a worker race-reads
	// b2Body internals.
	std::optional<EntityHandle> Physics2D::OverlapCircle(const Vec2& center, float radius, OverlapMode mode) {
		return ToEntityHandle(OverlapCircleRef(center, radius, mode));
	}
	std::optional<PhysicsBodyRef2D> Physics2D::OverlapCircleRef(const Vec2& center, float radius, OverlapMode mode) {
		// TODO: assert main thread when Application::IsMainThread() lands.
		b2ShapeProxy proxy{};
		proxy.count = 1;
		proxy.points[0] = { center.x, center.y };
		proxy.radius = radius;

		return QueryProxyRef(center, proxy, mode);
	}
	std::optional<EntityHandle> Physics2D::OverlapBox(const Vec2& center, const Vec2& halfExtents, float degrees, OverlapMode mode) {
		// TODO: assert main thread when Application::IsMainThread() lands.
		return ToEntityHandle(OverlapBoxRef(center, halfExtents, degrees, mode));
	}
	std::optional<PhysicsBodyRef2D> Physics2D::OverlapBoxRef(const Vec2& center, const Vec2& halfExtents, float degrees, OverlapMode mode) {
		// TODO: assert main thread when Application::IsMainThread() lands.
		float radians = Radians<float>(degrees);

		Vec2 corners[4] = {
			{ halfExtents.x,  halfExtents.y},
			{-halfExtents.x,  halfExtents.y},
			{-halfExtents.x, -halfExtents.y},
			{ halfExtents.x, -halfExtents.y}
		};
		Mat2 rot = {
			{ Cos(radians), -Sin(radians) },
			{ Sin(radians),  Cos(radians) }
		};

		b2ShapeProxy proxy{};
		proxy.count = 4;
		proxy.radius = 0.0f;
		for (int i = 0; i < 4; ++i) {
			Vec2 w = rot * corners[i] + Vec2{ center.x, center.y };
			proxy.points[i] = { w.x, w.y };
		}

		return QueryProxyRef(center, proxy, mode);
	}
	std::optional<EntityHandle> Physics2D::OverlapPolygon(const Vec2& center, const std::vector<Vec2>& points, OverlapMode mode) {
		// TODO: assert main thread when Application::IsMainThread() lands.
		return ToEntityHandle(OverlapPolygonRef(center, points, mode));
	}
	std::optional<PhysicsBodyRef2D> Physics2D::OverlapPolygonRef(const Vec2& center, const std::vector<Vec2>& points, OverlapMode mode) {
		// TODO: assert main thread when Application::IsMainThread() lands.
		if (points.size() < 3 || points.size() > B2_MAX_POLYGON_VERTICES) {
			return std::nullopt;
		}

		b2ShapeProxy proxy{};
		proxy.count = static_cast<int>(points.size());
		proxy.radius = 0.0f;
		for (int i = 0; i < proxy.count; ++i) {
			proxy.points[i] = { center.x + points[static_cast<size_t>(i)].x, center.y + points[static_cast<size_t>(i)].y };
		}

		return QueryProxyRef(center, proxy, mode);
	}
	std::optional<EntityHandle> Physics2D::ContainsPoint(const Vec2& point, OverlapMode mode) {
		// TODO: assert main thread when Application::IsMainThread() lands.
		return ToEntityHandle(ContainsPointRef(point, mode));
	}
	std::optional<PhysicsBodyRef2D> Physics2D::ContainsPointRef(const Vec2& point, OverlapMode mode) {
		// TODO: assert main thread when Application::IsMainThread() lands.
		return QueryPointRef(point, mode);
	}
	std::optional<RaycastHit2D> Physics2D::Raycast(const Vec2& origin,const Vec2& direction, float maxDistance) {
		// TODO: assert main thread when Application::IsMainThread() lands.
		if (!PhysicsSystem2D::IsInitialized() || !PhysicsSystem2D::IsEnabled() || maxDistance <= 0.0f) {
			return std::nullopt;
		}

		// Reject zero direction up-front — Normalized below would otherwise
		// produce NaN, which Box2D would happily ray-cast to garbage.
		if (direction.x == 0.0f && direction.y == 0.0f) {
			return std::nullopt;
		}

		auto& phys = PhysicsSystem2D::GetMainPhysicsWorld();
		b2WorldId world = phys.m_WorldId;

		b2Vec2 o{ origin.x, origin.y };
		Vec2 nd = Normalized(direction);
		b2Vec2 t{ nd.x * maxDistance, nd.y * maxDistance };

		b2QueryFilter filter = b2DefaultQueryFilter();

		b2RayResult r = b2World_CastRayClosest(world, o, t, filter);

		if (!b2Shape_IsValid(r.shapeId))
			return std::nullopt;

		PhysicsBodyRef2D ref = ResolveBodyRefFromShape(phys, r.shapeId);
		if (!IsValidRef(ref)) {
			return std::nullopt;
		}

		RaycastHit2D hit;
		hit.entity = ref.entity;
		hit.scene = ref.scene;
		hit.point = { r.point.x, r.point.y };
		hit.normal = { r.normal.x, r.normal.y };
		hit.distance = r.fraction * maxDistance;
		return hit;
	}

	std::vector<EntityHandle> Physics2D::OverlapCircleAll(const Vec2& center, float radius) {
		// TODO: assert main thread when Application::IsMainThread() lands.
		return ToEntityHandles(OverlapCircleAllRefs(center, radius));
	}
	std::vector<PhysicsBodyRef2D> Physics2D::OverlapCircleAllRefs(const Vec2& center, float radius) {
		// TODO: assert main thread when Application::IsMainThread() lands.
		b2ShapeProxy proxy{};
		proxy.count = 1;
		proxy.points[0] = { center.x, center.y };
		proxy.radius = radius;

		return QueryProxyAllRefs(proxy);
	}
	std::vector<EntityHandle> Physics2D::OverlapBoxAll(const Vec2& center, const Vec2& halfExtents, float degrees) {
		// TODO: assert main thread when Application::IsMainThread() lands.
		return ToEntityHandles(OverlapBoxAllRefs(center, halfExtents, degrees));
	}
	std::vector<PhysicsBodyRef2D> Physics2D::OverlapBoxAllRefs(const Vec2& center, const Vec2& halfExtents, float degrees) {
		// TODO: assert main thread when Application::IsMainThread() lands.
		float radians = Radians<float>(degrees);

		Vec2 corners[4] = {
			{ halfExtents.x,  halfExtents.y},
			{-halfExtents.x,  halfExtents.y},
			{-halfExtents.x, -halfExtents.y},
			{ halfExtents.x, -halfExtents.y}
		};
		Mat2 rot = {
			{ Cos(radians), -Sin(radians) },
			{ Sin(radians),  Cos(radians) }
		};

		b2ShapeProxy proxy{};
		proxy.count = 4;
		proxy.radius = 0.0f;
		for (int i = 0; i < 4; ++i) {
			Vec2 w = rot * corners[i] + Vec2{ center.x, center.y };
			proxy.points[i] = { w.x, w.y };
		}

		return QueryProxyAllRefs(proxy);
	}
	std::vector<EntityHandle> Physics2D::OverlapPolygonAll(const Vec2& center, const std::vector<Vec2>& points) {
		// TODO: assert main thread when Application::IsMainThread() lands.
		return ToEntityHandles(OverlapPolygonAllRefs(center, points));
	}
	std::vector<PhysicsBodyRef2D> Physics2D::OverlapPolygonAllRefs(const Vec2& center, const std::vector<Vec2>& points) {
		// TODO: assert main thread when Application::IsMainThread() lands.
		if (points.size() < 3 || points.size() > B2_MAX_POLYGON_VERTICES) {
			return {};
		}

		b2ShapeProxy proxy{};
		proxy.count = static_cast<int>(points.size());
		proxy.radius = 0.0f;
		for (int i = 0; i < proxy.count; ++i) {
			proxy.points[i] = { center.x + points[static_cast<size_t>(i)].x, center.y + points[static_cast<size_t>(i)].y };
		}

		return QueryProxyAllRefs(proxy);
	}
	std::vector<EntityHandle> Physics2D::ContainsPointAll(const Vec2& point) {
		// TODO: assert main thread when Application::IsMainThread() lands.
		return ToEntityHandles(ContainsPointAllRefs(point));
	}
	std::vector<PhysicsBodyRef2D> Physics2D::ContainsPointAllRefs(const Vec2& point) {
		// TODO: assert main thread when Application::IsMainThread() lands.
		return QueryPointAllRefs(point);
	}
}
