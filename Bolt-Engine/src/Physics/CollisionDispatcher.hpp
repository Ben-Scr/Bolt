#pragma once
#include <box2d/box2d.h>
#include "Physics/Collision2D.hpp"
#include "Physics/PhysicsUtility.hpp"

#include <unordered_map>
#include <functional>
#include <vector>
#include <cstdint>

namespace Bolt {
	using ContactBeginCallback = std::function<void(const Collision2D&)>;
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
		}
		void Clear() {
			m_begin.clear();
			m_end.clear();
			m_hit.clear();
		}

		void Process(b2WorldId world, const ContactBeginCallback& onBegin = {}, const ContactEndCallback& onEnd = {}) {
			b2ContactEvents ev = b2World_GetContactEvents(world);

			for (int i = 0; i < ev.beginCount; ++i) {
				auto& e = ev.beginEvents[i];
				auto collision2D = Collision2D{ .entityA = PhysicsUtility::GetEntityHandleFromShapeID(e.shapeIdA),.entityB = PhysicsUtility::GetEntityHandleFromShapeID(e.shapeIdB) };
				if (onBegin) onBegin(collision2D);
				Dispatch(e.shapeIdA, collision2D, m_begin);
				Dispatch(e.shapeIdB, collision2D, m_begin);
			}

			for (int i = 0; i < ev.endCount; ++i) {
				auto& e = ev.endEvents[i];
				auto collision2D = Collision2D{ .entityA = PhysicsUtility::GetEntityHandleFromShapeID(e.shapeIdA),.entityB = PhysicsUtility::GetEntityHandleFromShapeID(e.shapeIdB) };
				if (onEnd) onEnd(collision2D);
				Dispatch(e.shapeIdA, collision2D, m_end);
				Dispatch(e.shapeIdB, collision2D, m_end);
			}

			for (int i = 0; i < ev.hitCount; ++i) {
				auto& e = ev.hitEvents[i];
				auto collision2D = Collision2D{ .entityA = PhysicsUtility::GetEntityHandleFromShapeID(e.shapeIdA),.entityB = PhysicsUtility::GetEntityHandleFromShapeID(e.shapeIdB) };
				Dispatch(e.shapeIdA, collision2D, m_hit);
				Dispatch(e.shapeIdB, collision2D, m_hit);
			}
		}

	private:
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
	};
}
