#pragma once

#include "Events/AxiomEvent.hpp"

namespace Axiom {

	class WindowFocusEvent : public AxiomEvent {
	public:
		WindowFocusEvent() = default;

		AIM_EVENT_CLASS_TYPE(WindowFocus)
		AIM_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

} // namespace Axiom
