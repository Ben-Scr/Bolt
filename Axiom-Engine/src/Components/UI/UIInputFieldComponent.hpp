#pragma once

#include <string>

namespace Axiom {

	// Text input widget state. Pairs with a TextRenderer child entity
	// that renders Text (or PlaceholderText when Text is empty and the
	// field isn't focused). UIEventSystem owns the focus state machine:
	//
	//   - Click inside the field's RectTransform → IsFocused = true
	//   - Click anywhere else → IsFocused = false
	//   - While focused, the system appends typed characters to Text
	//     and treats Enter as a "submit" (sets SubmittedThisFrame).
	//
	// The character source is the same global Input layer used by the
	// rest of the engine; no separate input event queue is needed.
	struct UIInputFieldComponent {
		std::string Text;
		std::string PlaceholderText = "Enter text...";

		bool IsFocused = false;
		bool SubmittedThisFrame = false;     // Enter pressed while focused
		int CharacterLimit = 0;              // 0 = unlimited
	};

}
