#pragma once
#include "Core/Export.hpp"
#include "Collections/Vec2.hpp"

#include <Body.hpp>
#include <BodyType.hpp>

namespace Bolt {

	/// Physics body component using the Bolt-Physics library (lightweight alternative to Box2D).
	/// Use this for simple AABB-based collision and movement. For advanced physics (rotation,
	/// friction, continuous detection), use the standard Rigidbody2DComponent instead.
	struct BOLT_API FastBody2DComponent {
		BoltPhys::BodyType Type = BoltPhys::BodyType::Dynamic;
		float Mass = 1.0f;
		bool UseGravity = true;
		bool BoundaryCheck = false;

		// Runtime pointer — set by scene hooks, not serialized
		BoltPhys::Body* m_Body = nullptr;

		bool IsValid() const { return m_Body != nullptr; }

		Vec2 GetPosition() const {
			if (!m_Body) return {};
			auto p = m_Body->GetPosition();
			return { p.x, p.y };
		}

		void SetPosition(const Vec2& pos) {
			if (m_Body) m_Body->SetPosition({ pos.x, pos.y });
		}

		Vec2 GetVelocity() const {
			if (!m_Body) return {};
			auto v = m_Body->GetVelocity();
			return { v.x, v.y };
		}

		void SetVelocity(const Vec2& vel) {
			if (m_Body) m_Body->SetVelocity({ vel.x, vel.y });
		}
	};

}
