#pragma once

#include "Events/AxiomEvent.hpp"

namespace Axiom {

	class WindowLostFocusEvent : public AxiomEvent {
	public:
		WindowLostFocusEvent() = default;

		AIM_EVENT_CLASS_TYPE(WindowLostFocus)
		AIM_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

} // namespace Axiom
