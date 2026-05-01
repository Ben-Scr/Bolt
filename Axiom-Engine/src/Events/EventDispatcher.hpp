#pragma once

#include "Events/AxiomEvent.hpp"

namespace Axiom {

	class EventDispatcher {
	public:
		explicit EventDispatcher(AxiomEvent& event)
			: m_Event(event) {
		}

		template<typename T, typename F>
		bool Dispatch(F&& func) {
			if (m_Event.GetEventType() == T::GetStaticType() && !m_Event.Handled) {
				m_Event.Handled = func(static_cast<T&>(m_Event));
				return true;
			}
			return false;
		}

	private:
		AxiomEvent& m_Event;
	};

} // namespace Axiom
