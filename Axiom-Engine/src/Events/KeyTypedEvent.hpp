#pragma once

#include "Events/KeyEvent.hpp"

namespace Axiom {

	class KeyTypedEvent : public KeyEvent {
	public:
		explicit KeyTypedEvent(int keyCode)
			: KeyEvent(keyCode) {
		}

		AIM_EVENT_CLASS_TYPE(KeyTyped)
	};

} // namespace Axiom
