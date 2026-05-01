#pragma once

#include "Events/AxiomEvent.hpp"

namespace Axiom {

	class WindowMinimizeEvent : public AxiomEvent {
	public:
		explicit WindowMinimizeEvent(bool minimized)
			: m_Minimized(minimized) {
		}

		bool IsMinimized() const { return m_Minimized; }

		AIM_EVENT_CLASS_TYPE(WindowMinimize)
		AIM_EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:
		bool m_Minimized;
	};

} // namespace Axiom
