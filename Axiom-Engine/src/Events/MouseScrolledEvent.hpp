#pragma once

#include "Events/AxiomEvent.hpp"

namespace Axiom {

	class MouseScrolledEvent : public AxiomEvent {
	public:
		MouseScrolledEvent(float xOffset, float yOffset)
			: m_XOffset(xOffset), m_YOffset(yOffset) {
		}

		float GetXOffset() const { return m_XOffset; }
		float GetYOffset() const { return m_YOffset; }

		std::string ToString() const override {
			return std::string("MouseScrolledEvent: ") + std::to_string(m_XOffset) + ", " + std::to_string(m_YOffset);
		}

		AIM_EVENT_CLASS_TYPE(MouseScrolled)
		AIM_EVENT_CLASS_CATEGORY(EventCategoryInput | EventCategoryMouse)

	private:
		float m_XOffset;
		float m_YOffset;
	};

} // namespace Axiom
