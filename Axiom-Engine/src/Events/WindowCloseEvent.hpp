#pragma once

#include "Events/AxiomEvent.hpp"

namespace Axiom {

	class WindowCloseEvent : public AxiomEvent {
	public:
		WindowCloseEvent() = default;

		AIM_EVENT_CLASS_TYPE(WindowClose)
		AIM_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

} // namespace Axiom
