#pragma once
#include "Core/Export.hpp"
#include "Collections/Vec2.hpp"

#include <BoxCollider.hpp>

namespace Axiom {

	/// Box collider component using the Axiom-Physics library.
	/// Provides simple AABB-based collision detection. For polygon-based collision
	/// with friction and restitution, use the standard BoxCollider2DComponent instead.
	struct AXIOM_API FastBoxCollider2DComponent {
		Vec2 HalfExtents{ 0.5f, 0.5f };

		// Runtime pointer — set by scene hooks, not serialized
		AxiomPhys::BoxCollider* m_Collider = nullptr;

		bool IsValid() const { return m_Collider != nullptr; }

		Vec2 GetHalfExtents() const {
			if (m_Collider) {
				auto he = m_Collider->GetHalfExtents();
				return { he.x, he.y };
			}
			return HalfExtents;
		}

		void SetHalfExtents(const Vec2& he) {
			HalfExtents = he;
			if (m_Collider) m_Collider->SetHalfExtents({ he.x, he.y });
		}
	};

}
