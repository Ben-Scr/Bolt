#pragma once

#include "Events/MouseButtonEvent.hpp"

namespace Axiom {

	class MouseButtonReleasedEvent : public MouseButtonEvent {
	public:
		explicit MouseButtonReleasedEvent(int button)
			: MouseButtonEvent(button) {
		}

		std::string ToString() const override {
			return std::string("MouseButtonReleasedEvent: ") + std::to_string(m_Button);
		}

		AIM_EVENT_CLASS_TYPE(MouseButtonReleased)
	};

} // namespace Axiom
