#pragma once

#include <string>
#include <vector>

namespace Axiom {

	// Dropdown / combo box widget state. The button portion is the
	// entity itself; the option list is opened conceptually as a popup
	// (rendered by the UIRenderer when IsOpen is true) but lives in
	// the same component to keep authoring simple — no separate
	// "options panel" entity is required.
	//
	// SelectedIndex is the persisted choice. SelectionChangedThisFrame
	// is set by UIEventSystem on the frame the user picks a new option,
	// then cleared at the start of the next tick.
	struct DropdownComponent {
		std::vector<std::string> Options;
		int SelectedIndex = 0;

		bool IsOpen = false;
		bool SelectionChangedThisFrame = false;
	};

}
