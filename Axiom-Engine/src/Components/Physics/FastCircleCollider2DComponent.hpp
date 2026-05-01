#pragma once
#include "Core/Export.hpp"

#include <CircleCollider.hpp>

namespace Axiom {

	/// Circle collider component using the Axiom-Physics library.
	struct AXIOM_API FastCircleCollider2DComponent {
		float Radius = 0.5f;

		// Runtime pointer — set by scene hooks, not serialized
		AxiomPhys::CircleCollider* m_Collider = nullptr;

		bool IsValid() const { return m_Collider != nullptr; }

		float GetRadius() const {
			return m_Collider ? m_Collider->GetRadius() : Radius;
		}

		void SetRadius(float r) {
			Radius = r;
			if (m_Collider) m_Collider->SetRadius(r);
		}
	};

}
