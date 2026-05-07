#pragma once
#include <box2d/box2d.h>
#include "Physics/Collision2D.hpp"

#include <unordered_map>
#include <functional>
#include <vector>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace Axiom {
	using ContactBeginCallback = std::function<void(const Collision2D&)>;
	using ContactStayCallback = std::function<void(const Collision2D&)>;
	using ContactEndCallback = std::function<void(const Collision2D&)>;
	using ContactHitCallback = std::function<void(const Collision2D&)>;

	struct CollisionBodyRef {
		EntityHandle Entity = entt::null;
		Scene* OwningScene = nullptr;
	};

	using ShapeResolver = std::function<CollisionBodyRef(b2ShapeId)>;

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

		// Snapshot all registered begin/end/hit callbacks for a shape into a pod struct
		// the caller can restore later. Used to bridge BoxCollider2DComponent::SetSensor:
		// the underlying b2Shape gets recreated (changing its id), but the user-
		// registered collision callbacks should survive. Caller pattern is
		//   auto saved = dispatcher.SnapshotCallbacks(oldShape);
		//   ... DestroyShape() / CreateShape() ...
		//   dispatcher.RestoreCallbacks(newShape, std::move(saved));
		struct CallbackSnapshot {
			std::vector<ContactBeginCallback> Begin;
			std::vector<ContactEndCallback> End;
			std::vector<ContactHitCallback> Hit;
		};

		CallbackSnapshot SnapshotCallbacks(b2ShapeId id) {
			CallbackSnapshot snapshot;
			if (auto it = m_begin.find(id); it != m_begin.end()) snapshot.Begin = std::move(it->second);
			if (auto it = m_end.find(id);   it != m_end.end())   snapshot.End   = std::move(it->second);
			if (auto it = m_hit.find(id);   it != m_hit.end())   snapshot.Hit   = std::move(it->second);
			return snapshot;
		}

		void RestoreCallbacks(b2ShapeId id, CallbackSnapshot&& snapshot) {
			if (!snapshot.Begin.empty()) {
				auto& dest = m_begin[id];
				dest.insert(dest.end(),
					std::make_move_iterator(snapshot.Begin.begin()),
					std::make_move_iterator(snapshot.Begin.end()));
			}
			if (!snapshot.End.empty()) {
				auto& dest = m_end[id];
				dest.insert(dest.end(),
					std::make_move_iterator(snapshot.End.begin()),
					std::make_move_iterator(snapshot.End.end()));
			}
			if (!snapshot.Hit.empty()) {
				auto& dest = m_hit[id];
				dest.insert(dest.end(),
					std::make_move_iterator(snapshot.Hit.begin()),
					std::make_move_iterator(snapshot.Hit.end()));
			}
		}

		void Clear() {
			m_begin.clear();
			m_end.clear();
			m_hit.clear();
			m_activeContacts.clear();
		}

		void Process(
			b2WorldId world,
			const ShapeResolver& resolveShape,
			const ContactBeginCallback& onBegin = {},
			const ContactEndCallback& onEnd = {},
			const ContactStayCallback& onStay = {}) {
			b2ContactEvents ev = b2World_GetContactEvents(world);

			// Callbacks may destroy entities and re-enter UnregisterShape; DispatchSafe snapshots first.
			for (int i = 0; i < ev.beginCount; ++i) {
				auto& e = ev.beginEvents[i];
				auto collision2D = MakeCollision(e.shapeIdA, e.shapeIdB, resolveShape);
				m_activeContacts[MakeContactKey(e.shapeIdA, e.shapeIdB)] = { e.shapeIdA, e.shapeIdB, collision2D };
				if (onBegin) onBegin(collision2D);
				DispatchSafe(e.shapeIdA, collision2D, m_begin);
				DispatchSafe(e.shapeIdB, collision2D, m_begin);
			}

			for (int i = 0; i < ev.endCount; ++i) {
				auto& e = ev.endEvents[i];
				auto collision2D = MakeCollision(e.shapeIdA, e.shapeIdB, resolveShape);
				const ContactKey key = MakeContactKey(e.shapeIdA, e.shapeIdB);
				if (auto active = m_activeContacts.find(key); active != m_activeContacts.end()) {
					collision2D = active->second.Collision;
					m_activeContacts.erase(active);
				}
				if (onEnd) onEnd(collision2D);
				DispatchSafe(e.shapeIdA, collision2D, m_end);
				DispatchSafe(e.shapeIdB, collision2D, m_end);
			}

			for (int i = 0; i < ev.hitCount; ++i) {
				auto& e = ev.hitEvents[i];
				auto collision2D = MakeCollision(e.shapeIdA, e.shapeIdB, resolveShape, Vec2{ e.point.x, e.point.y });
				const ContactKey key = MakeContactKey(e.shapeIdA, e.shapeIdB);
				if (auto active = m_activeContacts.find(key); active != m_activeContacts.end()) {
					active->second.Collision.contactPoint = collision2D.contactPoint;
				}
				DispatchSafe(e.shapeIdA, collision2D, m_hit);
				DispatchSafe(e.shapeIdB, collision2D, m_hit);
			}

			if (onStay) {
				// Snapshot because callbacks may destroy entities and invalidate active contacts.
				std::vector<Collision2D> stayCollisions;
				stayCollisions.reserve(m_activeContacts.size());
				for (const auto& [_, active] : m_activeContacts) {
					stayCollisions.push_back(active.Collision);
				}
				for (const Collision2D& c : stayCollisions) {
					onStay(c);
				}
			}
		}

	private:
		struct ActiveContact {
			b2ShapeId ShapeA;
			b2ShapeId ShapeB;
			Collision2D Collision;
		};

		struct ContactKey {
			uint64_t ShapeA = 0;
			uint64_t ShapeB = 0;

			bool operator==(const ContactKey& other) const noexcept {
				return ShapeA == other.ShapeA && ShapeB == other.ShapeB;
			}
		};

		struct ContactKeyHash {
			size_t operator()(const ContactKey& key) const noexcept {
				const size_t hashA = std::hash<uint64_t>()(key.ShapeA);
				const size_t hashB = std::hash<uint64_t>()(key.ShapeB);
				return hashA ^ (hashB + 0x9e3779b97f4a7c15ull + (hashA << 6) + (hashA >> 2));
			}
		};

		static ContactKey MakeContactKey(b2ShapeId a, b2ShapeId b) {
			uint64_t ia = b2StoreShapeId(a);
			uint64_t ib = b2StoreShapeId(b);
			if (ia > ib) {
				std::swap(ia, ib);
			}
			return ContactKey{ ia, ib };
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

		static Collision2D MakeCollision(
			b2ShapeId shapeA,
			b2ShapeId shapeB,
			const ShapeResolver& resolveShape,
			std::optional<Vec2> contactPoint = std::nullopt) {
			CollisionBodyRef bodyA = resolveShape ? resolveShape(shapeA) : CollisionBodyRef{};
			CollisionBodyRef bodyB = resolveShape ? resolveShape(shapeB) : CollisionBodyRef{};
			return Collision2D{
				.entityA = bodyA.Entity,
				.entityB = bodyB.Entity,
				.sceneA = bodyA.OwningScene,
				.sceneB = bodyB.OwningScene,
				.contactPoint = contactPoint.value_or(EstimateContactPoint(shapeA, shapeB))
			};
		}

		template<typename Evt, typename Map>
		void Dispatch(b2ShapeId id, const Evt& e, Map& map) {
			auto it = map.find(id);
			if (it == map.end()) return;
			for (auto& cb : it->second) cb(e);
		}

		// Reentrant-safe: callback may UnregisterShape mid-dispatch, so snapshot the vector first.
		template<typename Evt, typename Map>
		void DispatchSafe(b2ShapeId id, const Evt& e, Map& map) {
			auto it = map.find(id);
			if (it == map.end()) return;
			using CallbackVec = std::remove_reference_t<decltype(it->second)>;
			CallbackVec snapshot = it->second;
			for (auto& cb : snapshot) cb(e);
		}

		std::unordered_map<b2ShapeId,
			std::vector<ContactBeginCallback>,
			ShapeIdHash,
			ShapeIdEqual> m_begin;
		std::unordered_map<b2ShapeId, std::vector<ContactEndCallback>, ShapeIdHash, ShapeIdEqual> m_end;
		std::unordered_map<b2ShapeId, std::vector<ContactHitCallback>, ShapeIdHash, ShapeIdEqual> m_hit;
		std::unordered_map<ContactKey, ActiveContact, ContactKeyHash> m_activeContacts;
	};
}
