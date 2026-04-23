#include "pch.hpp"
#include "Systems/LauncherLayer.hpp"
#include "Project/BoltProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/SceneManager.hpp"
#include "Core/Application.hpp"
#include <Core/Version.hpp>
#include <Core/Log.hpp>
#include "Serialization/Path.hpp"
#include "Serialization/Directory.hpp"
#include "Utils/Process.hpp"
#include "Graphics/Renderer2D.hpp"
#include "Utils/Timer.hpp"
#include "Utils/StringHelper.hpp"
#include <imgui.h>
#include <filesystem>
#include <cstring>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <system_error>

#ifdef BT_PLATFORM_WINDOWS
#include <shobjidl.h>
#endif

namespace Bolt {

	// ── Relative Time Formatting ────────────────────────────────────

	static std::string FormatRelativeTime(const std::string& iso8601) {
		if (iso8601.empty()) return "";

		std::tm tm{};
		std::istringstream ss(iso8601);
		ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
		if (ss.fail()) return "";

		std::time_t then = std::mktime(&tm);
		std::time_t now = std::time(nullptr);
		double seconds = std::difftime(now, then);

		if (seconds < 0) return "Now";
		if (seconds < 5) return "Now";
		if (seconds < 60) return std::to_string(static_cast<int>(seconds)) + "s ago";

		int minutes = static_cast<int>(seconds / 60);
		if (minutes < 60) return std::to_string(minutes) + "m ago";

		int hours = minutes / 60;
		if (hours < 24) return std::to_string(hours) + "h ago";

		int days = hours / 24;
		if (days < 30) return std::to_string(days) + "d ago";

		int months = days / 30;
		int remainingDays = days % 30;
		if (months < 12) {
			if (remainingDays > 0)
				return std::to_string(months) + "mo " + std::to_string(remainingDays) + "d ago";
			return std::to_string(months) + "mo ago";
		}

		int years = months / 12;
		int remainingMonths = months % 12;
		if (remainingMonths > 0)
			return std::to_string(years) + "y " + std::to_string(remainingMonths) + "mo ago";
		return std::to_string(years) + "y ago";
	}

#ifdef BT_PLATFORM_WINDOWS
	static std::wstring Utf8ToWide(std::string_view utf8) {
		if (utf8.empty()) {
			return {};
		}

		const int requiredChars = MultiByteToWideChar(
			CP_UTF8, MB_ERR_INVALID_CHARS,
			utf8.data(), static_cast<int>(utf8.size()),
			nullptr, 0);
		if (requiredChars <= 0) {
			return {};
		}

		std::wstring wide(static_cast<size_t>(requiredChars), L'\0');
		if (MultiByteToWideChar(
			CP_UTF8, MB_ERR_INVALID_CHARS,
			utf8.data(), static_cast<int>(utf8.size()),
			wide.data(), requiredChars) != requiredChars) {
			return {};
		}

		return wide;
	}

	static std::string FormatWindowsError(DWORD errorCode) {
		return std::error_code(static_cast<int>(errorCode), std::system_category()).message();
	}
#endif

	// ── Lifecycle ───────────────────────────────────────────────────

	void LauncherLayer::OnAttach(Application& app) {
		if (app.GetRenderer2D()) {
			app.GetRenderer2D()->SetSkipBeginFrameRender(true);
		}
		Application::SetIsPlaying(false);

		m_Registry.Load();
		m_Registry.ValidateAll();
		m_Registry.Save();

		std::snprintf(m_NewProjectLocation, sizeof(m_NewProjectLocation), "%s",
			BoltProject::GetDefaultProjectsDir().c_str());

		BT_INFO_TAG("Launcher", "Bolt Launcher opened ({} project(s))", m_Registry.GetProjects().size());
	}

	void LauncherLayer::OnImGuiRender(Application& app) {
		(void)app;
		PollCreateProjectTask();
		RenderLauncherPanel();
	}

	void LauncherLayer::OnDetach(Application& app) {
		(void)app;
		ResetCreateProjectTask();
	}

	void LauncherLayer::ResetCreateProjectTask(bool clearWorker) {
		if (clearWorker && m_CreateTask.Worker.joinable()) {
			m_CreateTask.Worker.join();
		}

		std::scoped_lock lock(m_CreateTask.Mutex);
		m_CreateTask.Worker = std::thread();
		m_CreateTask.CreatedProject.reset();
		m_CreateTask.Error.clear();
		m_CreateTask.Stage = "Idle";
		m_CreateTask.Progress = 0.0f;
		m_CreateTask.BuildExitCode = 0;
		m_CreateTask.Running = false;
		m_CreateTask.Finished = false;
		m_CreateTask.Success = false;
		m_CreateTask.InitialBuildSucceeded = true;
	}

	void LauncherLayer::StartCreateProjectAsync(const std::string& name, const std::string& location) {
		ResetCreateProjectTask();

		m_CreateError.clear();
		m_CloseCreatePopup = false;
		m_IsCreating = true;

		{
			std::scoped_lock lock(m_CreateTask.Mutex);
			m_CreateTask.Stage = "Preparing project...";
			m_CreateTask.Progress = 0.02f;
			m_CreateTask.Running = true;
		}

		m_CreateTask.Worker = std::thread([this, name, location]() {
			auto updateProgress = [this](float progress, std::string_view stage) {
				std::scoped_lock lock(m_CreateTask.Mutex);
				m_CreateTask.Progress = progress;
				m_CreateTask.Stage = std::string(stage);
			};

			try {
				const std::string fullPath = Path::Combine(location, name);
				if (std::filesystem::exists(fullPath)) {
					std::scoped_lock lock(m_CreateTask.Mutex);
					m_CreateTask.Error = "Directory already exists: " + fullPath;
					m_CreateTask.Stage = "Project creation failed";
					m_CreateTask.Finished = true;
					m_CreateTask.Running = false;
					m_CreateTask.Success = false;
					return;
				}

				updateProgress(0.05f, "Creating project...");
				BoltProject project = BoltProject::Create(name, location, updateProgress);

				updateProgress(0.90f, "Compiling starter scripts...");
				const std::string buildConfiguration = BoltProject::GetActiveBuildConfiguration();
				Process::Result buildResult = Process::Run({
					"dotnet",
					"build",
					project.CsprojPath,
					"-c", buildConfiguration,
					"--nologo",
					"-v", "q",
					"-nowarn:CS8632",
					"-p:DefineConstants=" + BoltProject::BuildManagedDefineConstants("BOLT_EDITOR")
					});

				std::scoped_lock lock(m_CreateTask.Mutex);
				m_CreateTask.CreatedProject = project;
				m_CreateTask.BuildExitCode = buildResult.ExitCode;
				m_CreateTask.InitialBuildSucceeded = buildResult.Succeeded();
				m_CreateTask.Progress = 1.0f;
				m_CreateTask.Stage = buildResult.Succeeded()
					? "Project ready."
					: "Project ready. Initial script build failed.";
				m_CreateTask.Finished = true;
				m_CreateTask.Running = false;
				m_CreateTask.Success = true;
			}
			catch (const std::exception& e) {
				std::scoped_lock lock(m_CreateTask.Mutex);
				m_CreateTask.Error = e.what();
				m_CreateTask.Stage = "Project creation failed";
				m_CreateTask.Finished = true;
				m_CreateTask.Running = false;
				m_CreateTask.Success = false;
			}
		});
	}

	void LauncherLayer::PollCreateProjectTask() {
		bool finished = false;
		bool success = false;
		bool initialBuildSucceeded = true;
		int buildExitCode = 0;
		std::string error;
		std::optional<BoltProject> createdProject;

		{
			std::scoped_lock lock(m_CreateTask.Mutex);
			finished = m_CreateTask.Finished;
			if (!finished) {
				return;
			}

			success = m_CreateTask.Success;
			initialBuildSucceeded = m_CreateTask.InitialBuildSucceeded;
			buildExitCode = m_CreateTask.BuildExitCode;
			error = m_CreateTask.Error;
			createdProject = m_CreateTask.CreatedProject;
		}

		if (m_CreateTask.Worker.joinable()) {
			m_CreateTask.Worker.join();
		}

		m_IsCreating = false;

		if (!success) {
			m_CreateError = error;
			ResetCreateProjectTask(false);
			return;
		}

		if (!createdProject.has_value()) {
			m_CreateError = "Project creation finished without a project result.";
			ResetCreateProjectTask(false);
			return;
		}

		m_Registry.AddProject(createdProject->Name, createdProject->RootDirectory);
		m_Registry.Save();

		if (!initialBuildSucceeded) {
			BT_WARN_TAG("Launcher", "Project created, but the initial script build failed (exit code {}).", buildExitCode);
		}

		m_CloseCreatePopup = true;
		m_PendingCreatedProjectOpen = LauncherProjectEntry{ createdProject->Name, createdProject->RootDirectory };
		ResetCreateProjectTask(false);
	}

	// ── Main Panel ──────────────────────────────────────────────────

	void LauncherLayer::RenderLauncherPanel() {
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;

		ImGui::Begin("##Launcher", nullptr, flags);

		ImGui::TextUnformatted("Bolt Engine");
		ImGui::SameLine();
		ImGui::TextDisabled("%s", BT_VERSION);
		ImGui::Separator();
		ImGui::Spacing();

		// While opening a project, show a fullscreen loading overlay and block all interaction
		if (m_IsOpening) {
			float w = ImGui::GetContentRegionAvail().x;
			float h = ImGui::GetContentRegionAvail().y;
			ImVec2 center(ImGui::GetCursorScreenPos().x + w * 0.5f,
				ImGui::GetCursorScreenPos().y + h * 0.4f);

			ImGui::SetCursorScreenPos(ImVec2(center.x - 120, center.y - 20));
			ImGui::TextUnformatted("Opening project...");

			ImGui::SetCursorScreenPos(ImVec2(center.x - 120, center.y + 5));
			float elapsed = std::chrono::duration<float>(
				std::chrono::steady_clock::now() - m_OpenStartTime).count();
			ImGui::ProgressBar(fmodf(elapsed * 0.3f, 1.0f), ImVec2(240, 0), "");

			ImGui::SetCursorScreenPos(ImVec2(center.x - 120, center.y + 30));
			ImGui::TextDisabled("%s", m_OpeningProjectName.c_str());

			// L1: Dismiss after cooldown, then perform deferred re-sort
			if (elapsed >= 1.5f) {
				m_IsOpening = false;

				if (!m_DeferredUpdatePath.empty()) {
					m_Registry.UpdateLastOpened(m_DeferredUpdatePath);
					m_Registry.Save();
					m_DeferredUpdatePath.clear();
				}
			}

			ImGui::End();
			return; // Skip all other UI — blocks interaction completely
		}

		float panelWidth = ImGui::GetContentRegionAvail().x;
		float rightColWidth = 200.0f;

		// Left column: project list
		ImGui::BeginChild("##ProjectList", ImVec2(panelWidth - rightColWidth - 10, 0), true);
		RenderProjectList();
		ImGui::EndChild();

		ImGui::SameLine();

		// Right column: actions (L4: removed selection-dependent buttons)
		ImGui::BeginChild("##Actions", ImVec2(rightColWidth, 0));

		if (ImGui::Button("Create New Project", ImVec2(-1, 0))) {
			m_OpenCreatePopup = true;
			m_CreateError.clear();
			m_IsCreating = false;
			m_NewProjectName[0] = '\0';
		}

		ImGui::Spacing();

		if (ImGui::Button("Add Existing Project", ImVec2(-1, 0))) {
			BrowseForExistingProject();
		}

		if (!m_BrowseError.empty()) {
			ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", m_BrowseError.c_str());
		}

		// L3: Show open error if any
		if (!m_OpenError.empty()) {
			ImGui::Spacing();
			ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", m_OpenError.c_str());
		}

		ImGui::EndChild();

		// L6: Render create popup modal inside the launcher window context
		RenderCreateProjectPopup();

		ImGui::End();
	}

	// ── Project List ────────────────────────────────────────────────

	void LauncherLayer::RenderProjectList() {
		const auto& projects = m_Registry.GetProjects();

		if (projects.empty()) {
			ImGui::TextDisabled("No projects yet");
			ImGui::TextDisabled("Create a new project or add an existing one.");
			return;
		}

		int removeIndex = -1;

		for (int i = 0; i < static_cast<int>(projects.size()); i++) {
			const auto& entry = projects[i];

			ImGui::PushID(i);

			std::string timeStr = FormatRelativeTime(entry.lastOpened);

			ImVec2 cursorPos = ImGui::GetCursorScreenPos();
			float rowWidth = ImGui::GetContentRegionAvail().x;
			float buttonWidth = 60.0f;
			float rowHeight = 50.0f;

			// Draw an invisible item spanning the row for layout + right-click context
			ImGui::InvisibleButton("##Row", ImVec2(rowWidth - buttonWidth - 8, rowHeight));

			// Right-click context menu per item
			if (ImGui::BeginPopupContextItem("##RowCtx")) {
				if (ImGui::MenuItem("Remove from List")) {
					removeIndex = i;
				}
				ImGui::EndPopup();
			}

			// Draw hover/active highlight
			bool hovered = ImGui::IsItemHovered();
			if (hovered) {
				ImGui::GetWindowDrawList()->AddRectFilled(
					cursorPos,
					ImVec2(cursorPos.x + rowWidth, cursorPos.y + rowHeight),
					ImGui::GetColorU32(ImGuiCol_HeaderHovered));
			}

			// Draw name
			ImGui::SetCursorScreenPos(ImVec2(cursorPos.x + 8, cursorPos.y + 4));
			ImGui::TextUnformatted(entry.name.c_str());

			// Draw path
			ImGui::SetCursorScreenPos(ImVec2(cursorPos.x + 8, cursorPos.y + 24));
			ImGui::TextDisabled("%s", entry.path.c_str());

			// Draw relative time in upper-right (offset left to leave room for button)
			if (!timeStr.empty()) {
				float timeWidth = ImGui::CalcTextSize(timeStr.c_str()).x;
				ImGui::SetCursorScreenPos(ImVec2(
					cursorPos.x + rowWidth - buttonWidth - timeWidth - 20,
					cursorPos.y + 4));
				ImGui::TextDisabled("%s", timeStr.c_str());
			}

			// Per-item Open button on the right side
			ImGui::SetCursorScreenPos(ImVec2(
				cursorPos.x + rowWidth - buttonWidth - 4,
				cursorPos.y + (rowHeight - ImGui::GetFrameHeight()) * 0.5f));
			if (ImGui::Button("Open", ImVec2(buttonWidth, 0))) {
				Timer timer;
				OpenProject(entry);
				BT_INFO_TAG("Project", "Opening Took: " + StringHelper::ToString(timer));
			}

			// Advance cursor past the row
			ImGui::SetCursorScreenPos(ImVec2(cursorPos.x, cursorPos.y + rowHeight));

			ImGui::PopID();
		}

		// Deferred removal to avoid modifying the list during iteration
		if (removeIndex >= 0) {
			m_Registry.RemoveProject(projects[removeIndex].path);
			m_Registry.Save();
		}
	}

	// ── Create Project Popup ────────────────────────────────────────

	void LauncherLayer::RenderCreateProjectPopup() {
		if (m_OpenCreatePopup) {
			ImGui::OpenPopup("Create New Project");
			m_OpenCreatePopup = false;
		}

		ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(450, 0), ImGuiCond_Appearing);

		if (ImGui::BeginPopupModal("Create New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			float createProgress = 0.0f;
			std::string createStage;
			bool createTaskRunning = false;
			{
				std::scoped_lock lock(m_CreateTask.Mutex);
				createProgress = m_CreateTask.Progress;
				createStage = m_CreateTask.Stage;
				createTaskRunning = m_CreateTask.Running;
			}

			if (m_CloseCreatePopup) {
				m_CloseCreatePopup = false;
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();

				if (m_PendingCreatedProjectOpen.has_value()) {
					OpenProject(*m_PendingCreatedProjectOpen);
					m_PendingCreatedProjectOpen.reset();
				}
				return;
			}

			ImGui::Text("Project Name:");
			ImGui::SetNextItemWidth(-1);
			ImGui::InputText("##ProjName", m_NewProjectName, sizeof(m_NewProjectName));

			ImGui::Spacing();
			ImGui::Text("Location:");
			ImGui::SetNextItemWidth(-1);
			ImGui::InputText("##ProjLocation", m_NewProjectLocation, sizeof(m_NewProjectLocation));

			ImGui::Spacing();
			std::string preview = Path::Combine(m_NewProjectLocation, m_NewProjectName);
			ImGui::TextDisabled("Path: %s", preview.c_str());

			if (!m_CreateError.empty()) {
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", m_CreateError.c_str());
			}

			if (m_IsCreating || createTaskRunning) {
				ImGui::Spacing();
				ImGui::TextDisabled("%s", createStage.empty() ? "Creating project..." : createStage.c_str());
				ImGui::ProgressBar(createProgress, ImVec2(-1, 0));
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			bool canCreate = BoltProject::IsValidProjectName(m_NewProjectName) && !m_IsCreating;
			if (!canCreate) ImGui::BeginDisabled();

			if (ImGui::Button("Create", ImVec2(120, 0))) {
				Timer timer;
				StartCreateProjectAsync(std::string(m_NewProjectName), std::string(m_NewProjectLocation));
				BT_INFO_TAG("Project", "Queued async project creation in {}", StringHelper::ToString(timer));
			}

			if (!canCreate) ImGui::EndDisabled();

			ImGui::SameLine();
			if (m_IsCreating) {
				ImGui::BeginDisabled();
			}
			if (ImGui::Button(m_IsCreating ? "Working..." : "Cancel", ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
			}
			if (m_IsCreating) {
				ImGui::EndDisabled();
			}

			ImGui::EndPopup();
		}
	}

	// ── Open Project ────────────────────────────────────────────────
	// Launches the editor process but keeps the launcher open.

	void LauncherLayer::OpenProject(const LauncherProjectEntry& entry) {
		if (m_IsOpening) return; // Already opening a project -- ignore

		m_OpenError.clear();

#ifdef BT_PLATFORM_WINDOWS
		// L3: Check if this project is already open in another editor instance
		{
			auto it = m_RunningProjects.find(entry.path);
			if (it != m_RunningProjects.end()) {
				HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, it->second);
				if (hProc) {
					DWORD exitCode = 0;
					if (GetExitCodeProcess(hProc, &exitCode) && exitCode == STILL_ACTIVE) {
						CloseHandle(hProc);
						m_OpenError = "Project already open";
						return;
					}
					CloseHandle(hProc);
				}
				// Process is no longer running -- remove stale entry
				m_RunningProjects.erase(it);
			}
		}
#endif

#ifdef BT_PLATFORM_WINDOWS
		auto exeDir = std::filesystem::path(Path::ExecutableDir());
		auto editorExe = exeDir / "Bolt-Editor.exe";
		if (!std::filesystem::exists(editorExe)) {
			editorExe = exeDir / ".." / "Bolt-Editor" / "Bolt-Editor.exe";
		}

		if (std::filesystem::exists(editorExe)) {
			std::error_code canonicalError;
			std::filesystem::path resolvedEditorExe = std::filesystem::weakly_canonical(editorExe, canonicalError);
			if (canonicalError) {
				resolvedEditorExe = editorExe.lexically_normal();
			}

			const std::wstring projectPath = Utf8ToWide(entry.path);
			if (projectPath.empty() && !entry.path.empty()) {
				m_OpenError = "Project path is not valid UTF-8";
				BT_ERROR_TAG("Launcher", "Failed to open project '{}': invalid UTF-8 path", entry.path);
				return;
			}

			const std::wstring commandLine =
				L"\"" + resolvedEditorExe.native() + L"\" --project=\"" + projectPath + L"\"";
			std::vector<wchar_t> buf(commandLine.begin(), commandLine.end());
			buf.push_back(L'\0');

			const std::wstring workingDirectory = resolvedEditorExe.parent_path().native();

			STARTUPINFOW si{};
			si.cb = sizeof(si);
			PROCESS_INFORMATION pi{};

			if (CreateProcessW(resolvedEditorExe.c_str(), buf.data(), nullptr, nullptr,
				FALSE, CREATE_NEW_PROCESS_GROUP, nullptr,
				workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &si, &pi))
			{
				m_IsOpening = true;
				m_OpenStartTime = std::chrono::steady_clock::now();
				m_OpeningProjectName = entry.name;
				m_DeferredUpdatePath = entry.path;

				// L3: Store PID for duplicate-open detection
				m_RunningProjects[entry.path] = pi.dwProcessId;
				CloseHandle(pi.hProcess);
				CloseHandle(pi.hThread);

				BT_INFO_TAG("Launcher", "Opened project: {} at {}", entry.name, entry.path);
			}
			else {
				const DWORD errorCode = GetLastError();
				m_OpenError = "Failed to open project: " + FormatWindowsError(errorCode);
				BT_ERROR_TAG("Launcher", "CreateProcessW failed for '{}' with error {} ({})",
					entry.path, errorCode, m_OpenError);
			}
		}
		else {
			m_OpenError = "Bolt-Editor.exe not found";
			BT_ERROR_TAG("Launcher", "Bolt-Editor.exe not found");
		}
#endif
	}

	// ── Browse for Existing Project ─────────────────────────────────

	void LauncherLayer::BrowseForExistingProject() {
		m_BrowseError.clear();

#ifdef BT_PLATFORM_WINDOWS
		CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

		IFileOpenDialog* dialog = nullptr;
		HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
			IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog));

		if (SUCCEEDED(hr)) {
			DWORD options;
			dialog->GetOptions(&options);
			dialog->SetOptions(options | FOS_PICKFOLDERS);
			dialog->SetTitle(L"Select Bolt Project Folder");

			hr = dialog->Show(nullptr);
			if (SUCCEEDED(hr)) {
				IShellItem* result = nullptr;
				dialog->GetResult(&result);
				if (result) {
					PWSTR widePath = nullptr;
					result->GetDisplayName(SIGDN_FILESYSPATH, &widePath);
					if (widePath) {
						int len = WideCharToMultiByte(CP_UTF8, 0, widePath, -1, nullptr, 0, nullptr, nullptr);
						std::string path(len - 1, '\0');
						WideCharToMultiByte(CP_UTF8, 0, widePath, -1, path.data(), len, nullptr, nullptr);
						CoTaskMemFree(widePath);

						if (BoltProject::Validate(path)) {
							auto project = BoltProject::Load(path);
							m_Registry.AddProject(project.Name, project.RootDirectory);
							m_Registry.Save();
						}
						else {
							m_BrowseError = "Not a valid Bolt project (missing bolt-project.json or Assets/)";
						}
					}
					result->Release();
				}
			}
			dialog->Release();
		}

		CoUninitialize();
#endif
	}

}
