#pragma once

#include "Events/AxiomEvent.hpp"

namespace Axiom {

	class MouseButtonEvent : public AxiomEvent {
	public:
		int GetButton() const { return m_Button; }

		AIM_EVENT_CLASS_CATEGORY(EventCategoryInput | EventCategoryMouse | EventCategoryMouseButton)

	protected:
		explicit MouseButtonEvent(int button)
			: m_Button(button) {
		}

		int m_Button;
	};

} // namespace Axiom
