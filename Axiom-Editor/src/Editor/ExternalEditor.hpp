#pragma once
#include "Core/Export.hpp"
#include "Editor/ExternalEditorInfo.hpp"

#include <string>
#include <vector>

namespace Axiom {

	/// Detects and manages the external code editor used for opening scripts.
	class AXIOM_API ExternalEditor {
	public:
		/// Detect all available editors on the system.
		static void DetectEditors();

		/// Get the list of detected editors.
		static const std::vector<ExternalEditorInfo>& GetAvailableEditors() { return s_Editors; }

		/// Get/set the active editor. Index into GetAvailableEditors().
		static int GetSelectedIndex() { return s_SelectedIndex; }
		static void SetSelectedIndex(int index);
		static const ExternalEditorInfo& GetSelected();

		/// Open a file in the configured editor with full project context.
		/// Resolves the .sln / project folder automatically from the current AxiomProject.
		/// Line is 1-based (0 = don't jump to line).
		static void OpenFile(const std::string& filePath, int line = 0);

		/// Returns true if the given extension should be opened with the code editor
		/// rather than the OS default application.
		static bool IsScriptExtension(const std::string& ext);

	private:
		static std::string ResolveProjectContext(const std::string& filePath, const std::string& ext);
		static void SavePreferences();
		static void LoadPreferences();

	private:
		static void TryDetect(ExternalEditorType type, const std::string& displayName,
			const std::vector<std::string>& candidates);

		static std::vector<ExternalEditorInfo> s_Editors;
		static int s_SelectedIndex;
		static bool s_Detected;
	};

}
