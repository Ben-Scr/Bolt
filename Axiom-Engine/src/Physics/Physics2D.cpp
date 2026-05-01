#include "pch.hpp"

#include "Physics/Physics2D.hpp"
#include "Physics/PhysicsSystem2D.hpp"
#include "Physics/Box2DWorld.hpp"

#include <Collections/Mat2.hpp>
#include <Math/Common.hpp>

#include <box2d/box2d.h>
#include <box2d/types.h>
#include <box2d/collision.h>

namespace Axiom {
	namespace {
		EntityHandle GetEntityHandleFromShape(b2ShapeId shapeId) {
			b2BodyId bodyId = b2Shape_GetBody(shapeId);
			void* userData = b2Body_GetUserData(bodyId);
			return static_cast<EntityHandle>(reinterpret_cast<uintptr_t>(userData));
		}

		std::optional<EntityHandle> QueryProxy(const Vec2& center, const b2ShapeProxy& proxy, OverlapMode mode) {
			if (!PhysicsSystem2D::IsInitialized() || !PhysicsSystem2D::IsEnabled()) {
				return std::nullopt;
			}

			auto& phys = PhysicsSystem2D::GetMainPhysicsWorld();
			b2WorldId world = phys.GetWorldID();
			b2QueryFilter filter = b2DefaultQueryFilter();

			struct QueryContext {
				Vec2 center;
				OverlapMode mode;
				std::optional<EntityHandle> first;
				std::optional<EntityHandle> nearest;
				float bestDist2 = std::numeric_limits<float>::max();

				static bool Report(b2ShapeId shapeId, void* ctx) {
					auto* self = static_cast<QueryContext*>(ctx);
					EntityHandle handle = GetEntityHandleFromShape(shapeId);

					if (self->mode == OverlapMode::First) {
						self->first = handle;
						return false;
					}

					b2BodyId bodyId = b2Shape_GetBody(shapeId);
					b2Transform xf = b2Body_GetTransform(bodyId);
					float dx = xf.p.x - self->center.x;
					float dy = xf.p.y - self->center.y;
					float d2 = dx * dx + dy * dy;
					if (d2 < self->bestDist2) {
						self->bestDist2 = d2;
						self->nearest = handle;
					}
					return true;
				}
			} context{ center, mode, std::nullopt, std::nullopt };

			b2World_OverlapShape(world, &proxy, filter, QueryContext::Report, &context);
			return mode == OverlapMode::First ? context.first : context.nearest;
		}

		std::vector<EntityHandle> QueryProxyAll(const b2ShapeProxy& proxy) {
			std::vector<EntityHandle> results;
			if (!PhysicsSystem2D::IsInitialized() || !PhysicsSystem2D::IsEnabled()) {
				return results;
			}

			auto& phys = PhysicsSystem2D::GetMainPhysicsWorld();
			b2WorldId world = phys.GetWorldID();
			b2QueryFilter filter = b2DefaultQueryFilter();

			struct QueryContext {
				std::vector<EntityHandle>* out;
				static bool Report(b2ShapeId shapeId, void* ctx) {
					auto* self = static_cast<QueryContext*>(ctx);
					self->out->push_back(GetEntityHandleFromShape(shapeId));
					return true;
				}
			} context{ &results };

			b2World_OverlapShape(world, &proxy, filter, QueryContext::Report, &context);
			return results;
		}

		std::optional<EntityHandle> QueryPoint(const Vec2& point, OverlapMode mode) {
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
				std::optional<EntityHandle> first;
				std::optional<EntityHandle> nearest;
				float bestDist2 = std::numeric_limits<float>::max();

				static bool Report(b2ShapeId shapeId, void* ctx) {
					auto* self = static_cast<QueryContext*>(ctx);
					if (!b2Shape_TestPoint(shapeId, { self->point.x, self->point.y })) {
						return true;
					}

					EntityHandle handle = GetEntityHandleFromShape(shapeId);
					if (self->mode == OverlapMode::First) {
						self->first = handle;
						return false;
					}

					b2BodyId bodyId = b2Shape_GetBody(shapeId);
					b2Transform xf = b2Body_GetTransform(bodyId);
					float dx = xf.p.x - self->point.x;
					float dy = xf.p.y - self->point.y;
					float d2 = dx * dx + dy * dy;
					if (d2 < self->bestDist2) {
						self->bestDist2 = d2;
						self->nearest = handle;
					}
					return true;
				}
			} context{ point, mode, std::nullopt, std::nullopt };

			b2World_OverlapAABB(world, aabb, filter, QueryContext::Report, &context);
			return mode == OverlapMode::First ? context.first : context.nearest;
		}

		std::vector<EntityHandle> QueryPointAll(const Vec2& point) {
			std::vector<EntityHandle> results;
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
				std::vector<EntityHandle>* out;

				static bool Report(b2ShapeId shapeId, void* ctx) {
					auto* self = static_cast<QueryContext*>(ctx);
					if (b2Shape_TestPoint(shapeId, { self->point.x, self->point.y })) {
						self->out->push_back(GetEntityHandleFromShape(shapeId));
					}
					return true;
				}
			} context{ point, &results };

			b2World_OverlapAABB(world, aabb, filter, QueryContext::Report, &context);
			return results;
		}
	}

	std::optional<EntityHandle> Physics2D::OverlapCircle(const Vec2& center, float radius, OverlapMode mode) {
		b2ShapeProxy proxy{};
		proxy.count = 1;
		proxy.points[0] = { center.x, center.y };
		proxy.radius = radius;

		return QueryProxy(center, proxy, mode);
	}
	std::optional<EntityHandle> Physics2D::OverlapBox(const Vec2& center, const Vec2& halfExtents, float degrees, OverlapMode mode) {
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

		return QueryProxy(center, proxy, mode);
	}
	std::optional<EntityHandle> Physics2D::OverlapPolygon(const Vec2& center, const std::vector<Vec2>& points, OverlapMode mode) {
		if (points.size() < 3 || points.size() > B2_MAX_POLYGON_VERTICES) {
			return std::nullopt;
		}

		b2ShapeProxy proxy{};
		proxy.count = static_cast<int>(points.size());
		proxy.radius = 0.0f;
		for (int i = 0; i < proxy.count; ++i) {
			proxy.points[i] = { center.x + points[static_cast<size_t>(i)].x, center.y + points[static_cast<size_t>(i)].y };
		}

		return QueryProxy(center, proxy, mode);
	}
	std::optional<EntityHandle> Physics2D::ContainsPoint(const Vec2& point, OverlapMode mode) {
		return QueryPoint(point, mode);
	}
	std::optional<RaycastHit2D> Physics2D::Raycast(const Vec2& origin,const Vec2& direction, float maxDistance) {
		if (!PhysicsSystem2D::IsInitialized() || !PhysicsSystem2D::IsEnabled() || maxDistance <= 0.0f) {
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

		b2BodyId   bId = b2Shape_GetBody(r.shapeId);
		void* ud = b2Body_GetUserData(bId);
		uintptr_t i = reinterpret_cast<uintptr_t>(ud);
		EntityHandle e = static_cast<EntityHandle>(i);

		RaycastHit2D hit;
		hit.entity = e;
		hit.point = { r.point.x, r.point.y };
		hit.normal = { r.normal.x, r.normal.y };
		hit.distance = r.fraction * maxDistance;
		return hit;
	}

	std::vector<EntityHandle> Physics2D::OverlapCircleAll(const Vec2& center, float radius) {
		b2ShapeProxy proxy{};
		proxy.count = 1;
		proxy.points[0] = { center.x, center.y };
		proxy.radius = radius;

		return QueryProxyAll(proxy);
	}
	std::vector<EntityHandle> Physics2D::OverlapBoxAll(const Vec2& center, const Vec2& halfExtents, float degrees) {
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

		return QueryProxyAll(proxy);
	}
	std::vector<EntityHandle> Physics2D::OverlapPolygonAll(const Vec2& center, const std::vector<Vec2>& points) {
		if (points.size() < 3 || points.size() > B2_MAX_POLYGON_VERTICES) {
			return {};
		}

		b2ShapeProxy proxy{};
		proxy.count = static_cast<int>(points.size());
		proxy.radius = 0.0f;
		for (int i = 0; i < proxy.count; ++i) {
			proxy.points[i] = { center.x + points[static_cast<size_t>(i)].x, center.y + points[static_cast<size_t>(i)].y };
		}

		return QueryProxyAll(proxy);
	}
	std::vector<EntityHandle> Physics2D::ContainsPointAll(const Vec2& point) {
		return QueryPointAll(point);
	}
}
