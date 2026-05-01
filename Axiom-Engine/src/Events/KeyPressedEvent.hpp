#pragma once

#include "Events/KeyEvent.hpp"

namespace Axiom {

	class KeyPressedEvent : public KeyEvent {
	public:
		KeyPressedEvent(int keyCode, bool isRepeat = false)
			: KeyEvent(keyCode), m_IsRepeat(isRepeat) {
		}

		bool IsRepeat() const { return m_IsRepeat; }

		std::string ToString() const override {
			return std::string("KeyPressedEvent: ") + std::to_string(m_KeyCode) + " (repeat=" + (m_IsRepeat ? "true" : "false") + ")";
		}

		AIM_EVENT_CLASS_TYPE(KeyPressed)

	private:
		bool m_IsRepeat;
	};

} // namespace Axiom
