#pragma once
#include <box2d/box2d.h>
#include "Physics/Collision2D.hpp"
#include "Physics/PhysicsUtility.hpp"

#include <unordered_map>
#include <functional>
#include <vector>
#include <cstdint>
#include <optional>
#include <utility>

namespace Axiom {
	using ContactBeginCallback = std::function<void(const Collision2D&)>;
	using ContactStayCallback = std::function<void(const Collision2D&)>;
	using ContactEndCallback = std::function<void(const Collision2D&)>;
	using ContactHitCallback = std::function<void(const Collision2D&)>;
	
	struct ShapeIdHash {
		size_t operator()(b2ShapeId id) const noexcept {
			return std::hash<uint64_t>()(b2StoreShapeId(id));
		}
	};

	struct ShapeIdEqual {
		bool operator()(b2ShapeId a, b2ShapeId b) const noexcept {
			return b2StoreShapeId(a) == b2StoreShapeId(b);
		}
	};


	class CollisionDispatcher {
	public:
		void RegisterBegin(b2ShapeId id, ContactBeginCallback cb) {
			m_begin[id].push_back(std::move(cb));
		}
		void RegisterEnd(b2ShapeId id, ContactEndCallback cb) {
			m_end[id].push_back(std::move(cb));
		}
		void RegisterHit(b2ShapeId id, ContactHitCallback cb) {
			m_hit[id].push_back(std::move(cb));
		}
		void UnregisterShape(b2ShapeId id) {
			m_begin.erase(id);
			m_end.erase(id);
			m_hit.erase(id);
			for (auto it = m_activeContacts.begin(); it != m_activeContacts.end(); ) {
				if (ShapeIdEqual{}(it->second.ShapeA, id) || ShapeIdEqual{}(it->second.ShapeB, id)) {
					it = m_activeContacts.erase(it);
				}
				else {
					++it;
				}
			}
		}
		void Clear() {
			m_begin.clear();
			m_end.clear();
			m_hit.clear();
			m_activeContacts.clear();
		}

		void Process(b2WorldId world, const ContactBeginCallback& onBegin = {}, const ContactEndCallback& onEnd = {}, const ContactStayCallback& onStay = {}) {
			b2ContactEvents ev = b2World_GetContactEvents(world);

			for (int i = 0; i < ev.beginCount; ++i) {
				auto& e = ev.beginEvents[i];
				auto collision2D = MakeCollision(e.shapeIdA, e.shapeIdB);
				m_activeContacts[MakeContactKey(collision2D.entityA, collision2D.entityB)] = { e.shapeIdA, e.shapeIdB, collision2D };
				if (onBegin) onBegin(collision2D);
				Dispatch(e.shapeIdA, collision2D, m_begin);
				Dispatch(e.shapeIdB, collision2D, m_begin);
			}

			for (int i = 0; i < ev.endCount; ++i) {
				auto& e = ev.endEvents[i];
				auto collision2D = MakeCollision(e.shapeIdA, e.shapeIdB);
				const uint64_t key = MakeContactKey(collision2D.entityA, collision2D.entityB);
				if (auto active = m_activeContacts.find(key); active != m_activeContacts.end()) {
					collision2D = active->second.Collision;
					m_activeContacts.erase(active);
				}
				if (onEnd) onEnd(collision2D);
				Dispatch(e.shapeIdA, collision2D, m_end);
				Dispatch(e.shapeIdB, collision2D, m_end);
			}

			for (int i = 0; i < ev.hitCount; ++i) {
				auto& e = ev.hitEvents[i];
				auto collision2D = MakeCollision(e.shapeIdA, e.shapeIdB, Vec2{ e.point.x, e.point.y });
				const uint64_t key = MakeContactKey(collision2D.entityA, collision2D.entityB);
				if (auto active = m_activeContacts.find(key); active != m_activeContacts.end()) {
					active->second.Collision.contactPoint = collision2D.contactPoint;
				}
				Dispatch(e.shapeIdA, collision2D, m_hit);
				Dispatch(e.shapeIdB, collision2D, m_hit);
			}

			if (onStay) {
				for (const auto& [_, active] : m_activeContacts) {
					onStay(active.Collision);
				}
			}
		}

	private:
		struct ActiveContact {
			b2ShapeId ShapeA;
			b2ShapeId ShapeB;
			Collision2D Collision;
		};

		static uint64_t MakeContactKey(EntityHandle a, EntityHandle b) {
			uint32_t ia = static_cast<uint32_t>(a);
			uint32_t ib = static_cast<uint32_t>(b);
			if (ia > ib) {
				std::swap(ia, ib);
			}
			return (static_cast<uint64_t>(ia) << 32) | ib;
		}

		static Vec2 EstimateContactPoint(b2ShapeId shapeA, b2ShapeId shapeB) {
			b2BodyId bodyA = b2Shape_GetBody(shapeA);
			b2BodyId bodyB = b2Shape_GetBody(shapeB);
			b2Transform transformA = b2Body_GetTransform(bodyA);
			b2Transform transformB = b2Body_GetTransform(bodyB);
			return {
				(transformA.p.x + transformB.p.x) * 0.5f,
				(transformA.p.y + transformB.p.y) * 0.5f
			};
		}

		static Collision2D MakeCollision(b2ShapeId shapeA, b2ShapeId shapeB, std::optional<Vec2> contactPoint = std::nullopt) {
			return Collision2D{
				.entityA = PhysicsUtility::GetEntityHandleFromShapeID(shapeA),
				.entityB = PhysicsUtility::GetEntityHandleFromShapeID(shapeB),
				.contactPoint = contactPoint.value_or(EstimateContactPoint(shapeA, shapeB))
			};
		}

		template<typename Evt, typename Map>
		void Dispatch(b2ShapeId id, const Evt& e, Map& map) {
			auto it = map.find(id);
			if (it == map.end()) return;
			for (auto& cb : it->second) cb(e);
		}

		std::unordered_map<b2ShapeId,
			std::vector<ContactBeginCallback>,
			ShapeIdHash,
			ShapeIdEqual> m_begin;
		std::unordered_map<b2ShapeId, std::vector<ContactEndCallback>, ShapeIdHash, ShapeIdEqual> m_end;
		std::unordered_map<b2ShapeId, std::vector<ContactHitCallback>, ShapeIdHash, ShapeIdEqual> m_hit;
		std::unordered_map<uint64_t, ActiveContact> m_activeContacts;
	};
}
