#pragma once

#include "Events/AxiomEvent.hpp"

namespace Axiom {

	class KeyEvent : public AxiomEvent {
	public:
		int GetKeyCode() const { return m_KeyCode; }

		AIM_EVENT_CLASS_CATEGORY(EventCategoryInput | EventCategoryKeyboard)

	protected:
		explicit KeyEvent(int keyCode)
			: m_KeyCode(keyCode) {
		}

		int m_KeyCode;
	};

} // namespace Axiom
