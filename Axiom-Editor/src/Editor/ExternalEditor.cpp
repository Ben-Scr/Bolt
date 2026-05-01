#include "pch.hpp"
#include "Editor/ExternalEditor.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Project/ProjectManager.hpp"
#include "Project/AxiomProject.hpp"

#include <filesystem>

#ifdef AIM_PLATFORM_WINDOWS
#include <windows.h>
#include <shellapi.h>
#endif

namespace Axiom {

	std::vector<ExternalEditorInfo> ExternalEditor::s_Editors;
	int ExternalEditor::s_SelectedIndex = 0;
	bool ExternalEditor::s_Detected = false;

	bool ExternalEditor::IsScriptExtension(const std::string& ext) {
		return ext == ".cs" || ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp"
			|| ext == ".lua" || ext == ".py" || ext == ".js" || ext == ".ts"
			|| ext == ".json" || ext == ".xml" || ext == ".yaml" || ext == ".yml"
			|| ext == ".txt" || ext == ".md" || ext == ".cfg" || ext == ".ini"
			|| ext == ".glsl" || ext == ".hlsl" || ext == ".shader";
	}

	void ExternalEditor::TryDetect(ExternalEditorType type, const std::string& displayName,
		const std::vector<std::string>& candidates) {
		for (const auto& path : candidates) {
			if (std::filesystem::exists(path)) {
				ExternalEditorInfo info;
				info.Type = type;
				info.DisplayName = displayName;
				info.ExecutablePath = path;
				info.Available = true;
				s_Editors.push_back(std::move(info));
				return;
			}
		}
	}

	void ExternalEditor::DetectEditors() {
		if (s_Detected) return;
		s_Detected = true;
		s_Editors.clear();

#ifdef AIM_PLATFORM_WINDOWS
		// Visual Studio 2022 (all editions)
		TryDetect(ExternalEditorType::VisualStudio, "Visual Studio 2022", {
			"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\devenv.exe",
			"C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\Common7\\IDE\\devenv.exe",
			"C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\Common7\\IDE\\devenv.exe",
		});

		// VS Code
		{
			std::string localAppData;
			char* env = nullptr;
			size_t len = 0;
			if (_dupenv_s(&env, &len, "LOCALAPPDATA") == 0 && env) {
				localAppData = env;
				free(env);
			}

			std::vector<std::string> vscodePaths = {
				localAppData + "\\Programs\\Microsoft VS Code\\Code.exe",
				"C:\\Program Files\\Microsoft VS Code\\Code.exe",
			};

			// Also check PATH
			char pathBuf[MAX_PATH]{};
			DWORD found = SearchPathA(nullptr, "code.cmd", nullptr, MAX_PATH, pathBuf, nullptr);
			if (found > 0)
				vscodePaths.insert(vscodePaths.begin(), std::string(pathBuf));

			TryDetect(ExternalEditorType::VSCode, "Visual Studio Code", vscodePaths);
		}

		// JetBrains Rider
		{
			std::string localAppData;
			char* env = nullptr;
			size_t len = 0;
			if (_dupenv_s(&env, &len, "LOCALAPPDATA") == 0 && env) {
				localAppData = env;
				free(env);
			}

			std::vector<std::string> riderPaths;
			// Toolbox installations
			auto toolboxDir = std::filesystem::path(localAppData) / "JetBrains" / "Toolbox" / "apps" / "Rider";
			if (std::filesystem::exists(toolboxDir)) {
				for (const auto& entry : std::filesystem::directory_iterator(toolboxDir)) {
					if (!entry.is_directory()) continue;
					auto bin = entry.path() / "bin" / "rider64.exe";
					if (std::filesystem::exists(bin))
						riderPaths.push_back(bin.string());
				}
			}

			// Standard install
			riderPaths.push_back("C:\\Program Files\\JetBrains\\JetBrains Rider\\bin\\rider64.exe");

			if (!riderPaths.empty())
				TryDetect(ExternalEditorType::Rider, "JetBrains Rider", riderPaths);
		}
#endif

		if (s_Editors.empty()) {
			AIM_CORE_WARN_TAG("ExternalEditor", "No code editors detected on this system");
		}
		else {
			AIM_CORE_INFO_TAG("ExternalEditor", "Detected {} code editor(s):", s_Editors.size());
			for (size_t i = 0; i < s_Editors.size(); i++)
				AIM_CORE_INFO_TAG("ExternalEditor", "  [{}] {} ({})", i, s_Editors[i].DisplayName, s_Editors[i].ExecutablePath);
		}

		LoadPreferences();
	}

	static std::string GetPreferencesPath() {
		return Path::Combine(Path::GetSpecialFolderPath(SpecialFolder::LocalAppData), "Axiom", "editor-preferences.json");
	}

	void ExternalEditor::SavePreferences() {
		std::string path = GetPreferencesPath();
		auto dir = std::filesystem::path(path).parent_path();
		if (!std::filesystem::exists(dir))
			std::filesystem::create_directories(dir);

		const auto& selected = (s_SelectedIndex >= 0 && s_SelectedIndex < static_cast<int>(s_Editors.size()))
			? s_Editors[s_SelectedIndex].DisplayName : "";

		Json::Value root = Json::Value::MakeObject();
		root.AddMember("selectedEditor", selected);
		File::WriteAllText(path, Json::Stringify(root, true));
	}

	void ExternalEditor::LoadPreferences() {
		std::string path = GetPreferencesPath();
		if (!std::filesystem::exists(path)) return;

		Json::Value root;
		std::string parseError;
		if (!Json::TryParse(File::ReadAllText(path), root, &parseError) || !root.IsObject()) {
			AIM_CORE_WARN_TAG("ExternalEditor", "Failed to parse editor preferences '{}': {}", path, parseError);
			return;
		}

		const Json::Value* selectedEditorValue = root.FindMember("selectedEditor");
		if (!selectedEditorValue) {
			return;
		}
		std::string savedName = selectedEditorValue->AsStringOr();

		for (int i = 0; i < static_cast<int>(s_Editors.size()); i++) {
			if (s_Editors[i].DisplayName == savedName) {
				s_SelectedIndex = i;
				return;
			}
		}
	}

	void ExternalEditor::SetSelectedIndex(int index) {
		s_SelectedIndex = index;
		SavePreferences();
	}

	const ExternalEditorInfo& ExternalEditor::GetSelected() {
		DetectEditors();
		if (s_SelectedIndex >= 0 && s_SelectedIndex < static_cast<int>(s_Editors.size()))
			return s_Editors[s_SelectedIndex];

		static ExternalEditorInfo fallback;
		return fallback;
	}

	std::string ExternalEditor::ResolveProjectContext(const std::string& filePath, const std::string& ext) {
		AxiomProject* project = ProjectManager::GetCurrentProject();
		if (!project) return "";

		// C# scripts → project .sln
		if (ext == ".cs") {
			if (std::filesystem::exists(project->SlnPath))
				return std::filesystem::canonical(project->SlnPath).string();
		}

		// C++ native scripts → NativeScripts .sln
		if (ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp") {
			auto nativeSln = std::filesystem::path(project->NativeScriptsDir)
				/ "build" / (project->Name + "-NativeScripts.sln");
			if (std::filesystem::exists(nativeSln))
				return std::filesystem::canonical(nativeSln).string();
		}

		// Fallback: project .sln if available
		if (std::filesystem::exists(project->SlnPath))
			return std::filesystem::canonical(project->SlnPath).string();

		return "";
	}

#ifdef AIM_PLATFORM_WINDOWS
	// Check if Visual Studio specifically has a window open containing the search string.
	// Requires BOTH the search title AND "Visual Studio" to be in the window title,
	// to avoid false positives from the Axiom editor or other windows.
	static bool IsVisualStudioOpen(const std::string& searchTitle) {
		if (searchTitle.empty()) return false;
		std::wstring wSearch(searchTitle.begin(), searchTitle.end());
		std::wstring wVS = L"Visual Studio";
		bool found = false;

		struct Ctx { bool* found; std::wstring* search; std::wstring* vs; };
		Ctx ctx = { &found, &wSearch, &wVS };

		EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
			auto* c = reinterpret_cast<Ctx*>(lParam);
			wchar_t title[512]{};
			GetWindowTextW(hwnd, title, 512);
			if (wcsstr(title, c->search->c_str()) != nullptr &&
				wcsstr(title, c->vs->c_str()) != nullptr) {
				*c->found = true;
				return FALSE;
			}
			return TRUE;
		}, reinterpret_cast<LPARAM>(&ctx));
		return found;
	}
#endif

	void ExternalEditor::OpenFile(const std::string& filePath, int line) {
		DetectEditors();

		if (s_Editors.empty()) {
			AIM_CORE_WARN_TAG("ExternalEditor", "No code editor available, falling back to OS default");
#ifdef AIM_PLATFORM_WINDOWS
			std::wstring wpath(filePath.begin(), filePath.end());
			ShellExecuteW(nullptr, L"open", wpath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
			return;
		}

		const auto& editor = GetSelected();
		std::string absPath = std::filesystem::absolute(filePath).string();

		std::string ext = std::filesystem::path(filePath).extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

		// Resolve .sln path for VS/Rider, folder path for VS Code
		std::string slnPath = ResolveProjectContext(filePath, ext);
		std::string projectFolder;
		AxiomProject* project = ProjectManager::GetCurrentProject();
		if (project) projectFolder = project->RootDirectory;

		AIM_CORE_INFO_TAG("ExternalEditor", "Opening '{}' with {} (sln: {})",
			absPath, editor.DisplayName, slnPath.empty() ? "none" : slnPath);

#ifdef AIM_PLATFORM_WINDOWS
		std::string fullCmd;

		std::string projectName = project ? project->Name : "";

		switch (editor.Type) {
		case ExternalEditorType::VSCode: {
			// VS Code needs the FOLDER (not .sln) as primary argument for IntelliSense.
			// code "projectFolder" --reuse-window --goto "file:line:col"
			// If already open, --reuse-window makes VS Code focus the existing window.
			fullCmd = "\"" + editor.ExecutablePath + "\"";
			if (!projectFolder.empty())
				fullCmd += " \"" + projectFolder + "\"";
			fullCmd += " --reuse-window";
			if (line > 0)
				fullCmd += " --goto \"" + absPath + ":" + std::to_string(line) + ":1\"";
			else
				fullCmd += " --goto \"" + absPath + ":1:1\"";
			break;
		}

		case ExternalEditorType::VisualStudio: {
			// VS2022 approach:
			// 1. If VS already has this solution open (check window title for "Visual Studio" + project name):
			//    → devenv /edit "file" (reuses running instance)
			// 2. If VS is NOT open with this project:
			//    → First: launch devenv with the .sln to load the full solution
			//    → Then: after a delay, use devenv /edit to open the specific file
			//    This two-step approach is needed because /command "File.OpenFile"
			//    has quoting issues with paths containing spaces.
			bool vsHasProject = IsVisualStudioOpen(projectName);

			if (vsHasProject) {
				// VS already has it — just navigate to the file
				fullCmd = "\"" + editor.ExecutablePath + "\" /edit \"" + absPath + "\"";
			}
			else if (!slnPath.empty()) {
				// VS not open — launch solution, then open file after delay
				std::string devenvPath = editor.ExecutablePath;
				std::string sln = slnPath;
				std::string file = absPath;

				std::thread([devenvPath, sln, file]() {
					// Step 1: Open the solution
					std::string cmd1 = "\"" + devenvPath + "\" \"" + sln + "\"";
					std::wstring wcmd1(cmd1.begin(), cmd1.end());
					std::vector<wchar_t> buf1(wcmd1.begin(), wcmd1.end());
					buf1.push_back(L'\0');

					STARTUPINFOW si1{};
					si1.cb = sizeof(si1);
					PROCESS_INFORMATION pi1{};

					CreateProcessW(nullptr, buf1.data(), nullptr, nullptr,
						FALSE, CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si1, &pi1);
					if (pi1.hProcess) {
						CloseHandle(pi1.hProcess);
						CloseHandle(pi1.hThread);
					}

					// Step 2: Wait for VS to load, then open the file
					Sleep(4000);

					std::string cmd2 = "\"" + devenvPath + "\" /edit \"" + file + "\"";
					std::wstring wcmd2(cmd2.begin(), cmd2.end());
					std::vector<wchar_t> buf2(wcmd2.begin(), wcmd2.end());
					buf2.push_back(L'\0');

					STARTUPINFOW si2{};
					si2.cb = sizeof(si2);
					PROCESS_INFORMATION pi2{};

					CreateProcessW(nullptr, buf2.data(), nullptr, nullptr,
						FALSE, CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si2, &pi2);
					if (pi2.hProcess) {
						CloseHandle(pi2.hProcess);
						CloseHandle(pi2.hThread);
					}
				}).detach();

				// Return early — the thread handles everything
				return;
			}
			else {
				fullCmd = "\"" + editor.ExecutablePath + "\" /edit \"" + absPath + "\"";
			}
			break;
		}

		case ExternalEditorType::Rider: {
			// rider "Project.sln" "file.cs"
			// .sln is the PROJECT-LOAD argument (loads Solution Explorer + IntelliSense).
			// Second argument is the file to navigate to.
			// Rider auto-reuses if same .sln is already open.
			fullCmd = "\"" + editor.ExecutablePath + "\"";
			if (!slnPath.empty())
				fullCmd += " \"" + slnPath + "\"";
			if (line > 0)
				fullCmd += " --line " + std::to_string(line);
			fullCmd += " \"" + absPath + "\"";
			break;
		}

		default: {
			fullCmd = "\"" + editor.ExecutablePath + "\" \"" + absPath + "\"";
			break;
		}
		}

		// Launch async so we don't block the engine editor
		std::thread([fullCmd]() {
			std::wstring wcmd(fullCmd.begin(), fullCmd.end());
			std::vector<wchar_t> buf(wcmd.begin(), wcmd.end());
			buf.push_back(L'\0');

			STARTUPINFOW si{};
			si.cb = sizeof(si);
			PROCESS_INFORMATION pi{};

			if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr,
				FALSE, CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi))
			{
				CloseHandle(pi.hProcess);
				CloseHandle(pi.hThread);
			}
		}).detach();
#endif
	}

}
