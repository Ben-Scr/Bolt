#include <pch.hpp>
#include "Gui/PackageManagerPanel.hpp"

#include "Core/Log.hpp"
#include "Core/PackageHost.hpp"
#include "Project/AxiomProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Utils/Process.hpp"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>

#ifdef AIM_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace Axiom {

	namespace {

		std::string ToLower(std::string value) {
			std::transform(value.begin(), value.end(), value.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return value;
		}

		bool MatchesFilter(const std::string& haystack, const char* filter) {
			if (!filter || filter[0] == '\0') return true;
			return ToLower(haystack).find(ToLower(filter)) != std::string::npos;
		}

		// NuGet IDs are case-insensitive — dotnet may write different casing than the search API.
		bool EqualsIgnoreCase(const std::string& a, const std::string& b) {
			if (a.size() != b.size()) return false;
			for (size_t i = 0; i < a.size(); ++i) {
				const unsigned char ca = static_cast<unsigned char>(a[i]);
				const unsigned char cb = static_cast<unsigned char>(b[i]);
				if (std::tolower(ca) != std::tolower(cb)) return false;
			}
			return true;
		}

	} // namespace

	void PackageManagerPanel::Initialize(PackageManager* manager) {
		m_Manager = manager;
		m_AutomationTask = std::make_shared<AutomationTaskState>();
		m_DiskInstallTask = std::make_shared<DiskInstallTaskState>();
	}

	void PackageManagerPanel::Shutdown() {
		// Wait for both workers before tearing down the state they touch.
		if (m_AutomationWorker.joinable()) {
			m_AutomationWorker.join();
		}
		if (m_DiskInstallWorker.joinable()) {
			m_DiskInstallWorker.join();
		}
		// std::async-policy futures join in their destructors, so a still-running
		// search or install would otherwise stall the UI thread on shutdown by
		// blocking until dotnet returns. Drain explicitly here so we know exactly
		// when the wait happens (and can sequence it before we null out m_Manager,
		// which the worker may still be touching).
		if (m_OperationFuture.valid()) {
			m_OperationFuture.wait();
		}
		if (m_SearchFuture.valid()) {
			m_SearchFuture.wait();
		}
		m_Manager = nullptr;
		m_SearchResults.clear();
		m_InstalledNuGetPackages.clear();
		m_AllManifests.clear();
		m_AutomationTask.reset();
		m_DiskInstallTask.reset();
	}

	void PackageManagerPanel::Render() {
		// Poll async NuGet search/operation futures (legacy flow).
		if (m_IsSearching && m_SearchFuture.valid()) {
			if (m_SearchFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
				m_SearchResults = m_SearchFuture.get();
				m_IsSearching = false;
				m_StatusMessage = std::to_string(m_SearchResults.size()) + " NuGet results";
				m_StatusIsError = false;
			}
		}
		if (m_IsOperating && m_OperationFuture.valid()) {
			if (m_OperationFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
				auto result = m_OperationFuture.get();
				m_IsOperating = false;
				m_StatusMessage = result.Message;
				m_StatusIsError = !result.Success;

				// Refresh from disk now — dotnet may partially write the .csproj on failure.
				if (m_Manager) {
					m_InstalledNuGetPackages = m_Manager->GetInstalledPackages();
					m_InstalledNuGetDirty = false;

					for (auto& pkg : m_SearchResults) {
						bool installed = false;
						std::string installedVersion;
						for (const auto& inst : m_InstalledNuGetPackages) {
							if (EqualsIgnoreCase(pkg.Id, inst.Id)) {
								installed = true;
								installedVersion = inst.Version;
								break;
							}
						}
						pkg.IsInstalled = installed;
						pkg.InstalledVersion = installed ? installedVersion : "";
					}
				}

				if (m_Manager && m_Manager->NeedsReload()) {
					if (ScriptEngine::IsInitialized())
						ScriptEngine::ReloadAssemblies();
					m_Manager->ClearReloadFlag();
				}
			}
		}

		PollAutomationTask();
		PollDiskInstallTask();
		RefreshManifestsIfDirty();

		// Progress strip — above tab bar so it's visible from any tab.
		if (m_AutomationTask && m_AutomationTask->Running.load(std::memory_order_acquire)) {
			std::string stage;
			float progress = 0.0f;
			{
				std::scoped_lock lock(m_AutomationTask->Mutex);
				stage = m_AutomationTask->Stage;
				progress = m_AutomationTask->Progress;
			}
			ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "%s", stage.c_str());
			ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
			ImGui::Separator();
		}

		// Reserve bottom strip; each tab is a scrollable child so the tab bar stays anchored.
		const float statusHeight = !m_StatusMessage.empty() ? ImGui::GetFrameHeightWithSpacing() : 0.0f;

		if (ImGui::BeginTabBar("##PackageManagerTabs")) {
			if (ImGui::BeginTabItem("Search Packages")) {
				m_TabIndex = 0;
				ImGui::BeginChild("##SearchScroll", ImVec2(0.0f, -statusHeight), false);
				RenderSearchPackagesTab();
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("In Project")) {
				m_TabIndex = 1;
				ImGui::BeginChild("##InProjectScroll", ImVec2(0.0f, -statusHeight), false);
				RenderInstalledPackagesTab();
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		// Status strip — pinned to the bottom of the panel.
		if (m_IsOperating) {
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", m_StatusMessage.c_str());
		}
		else if (m_StatusIsError) {
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", m_StatusMessage.c_str());
		}
		else if (!m_StatusMessage.empty()) {
			ImGui::TextDisabled("%s", m_StatusMessage.c_str());
		}

		// Floating install-windows opened from the "+" menu.
		RenderGitInstallWindow();
		RenderNuGetInstallWindow();
		RenderNewPackageWindow();
	}

	void PackageManagerPanel::RefreshManifestsIfDirty() {
		if (!m_ManifestsDirty) return;
		AxiomProject* project = ProjectManager::GetCurrentProject();
		const std::string projectRoot = project ? project->RootDirectory : std::string{};
		m_AllManifests = AxiomPackageInstaller::EnumerateAll(projectRoot);
		m_ManifestsDirty = false;
	}

	// ── Search Packages tab ────────────────────────────────────────────────────────

	void PackageManagerPanel::RenderSearchPackagesTab() {
		// Filter row: input fills the row width minus the "+" button.
		const float plusWidth = ImGui::GetFrameHeight();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - plusWidth - ImGui::GetStyle().ItemInnerSpacing.x);
		ImGui::InputTextWithHint("##AxiomFilter", "Filter packages...",
			m_AxiomSearchFilterBuffer, sizeof(m_AxiomSearchFilterBuffer));
		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

		const bool automating = m_AutomationTask && m_AutomationTask->Running.load(std::memory_order_acquire);
		// "Create new package" works without a project (engine-developer flow targeting
		// `<repo>/packages/`). The other entries still need a project — they install
		// into the project's csproj / allow-list. Gate them individually inside the popup.
		const bool canOpenMenu = !m_IsOperating && !automating;
		const bool canInstallToProject = ProjectManager::GetCurrentProject() && canOpenMenu;
		if (!canOpenMenu) ImGui::BeginDisabled();
		if (ImGui::Button("+", ImVec2(plusWidth, plusWidth))) {
			ImGui::OpenPopup("##AddPackageMenu");
		}
		if (!canOpenMenu) ImGui::EndDisabled();
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip(canOpenMenu ? "Add a package" : "Wait for current operation to finish");
		}

		if (ImGui::BeginPopup("##AddPackageMenu")) {
			if (ImGui::MenuItem("Create new package...")) {
				m_NewPackageNameBuffer[0] = '\0';
				m_NewPackageDescriptionBuffer[0] = '\0';
				m_NewPackageLayerNative = true;
				m_NewPackageLayerStandalone = false;
				m_NewPackageLayerCsharp = false;
				m_NewPackageTarget = ProjectManager::GetCurrentProject() ? 1 : 0;
				m_NewPackageError.clear();
				m_ShowNewPackageWindow = true;
			}
			ImGui::Separator();
			if (!canInstallToProject) ImGui::BeginDisabled();
			if (ImGui::MenuItem("Install package from disk")) {
				HandleDiskInstall();
			}
			if (ImGui::MenuItem("Install package from git URL")) {
				m_GitHubUrlBuffer[0] = '\0';
				m_ShowGitInstallWindow = true;
			}
			if (ImGui::MenuItem("Install package from NuGet")) {
				m_ShowNuGetInstallWindow = true;
			}
			if (!canInstallToProject) ImGui::EndDisabled();
			ImGui::EndPopup();
		}

		ImGui::Spacing();

		ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
		if (ImGui::CollapsingHeader("Axiom Registry")) {
			RenderAxiomRegistrySection();
		}
	}

	void PackageManagerPanel::RenderAxiomRegistrySection() {
		ImGui::Indent();

		int shown = 0;
		for (const auto& manifest : m_AllManifests) {
			if (!manifest.IsEngine) continue;
			if (!MatchesFilter(manifest.Name, m_AxiomSearchFilterBuffer)) continue;
			RenderAxiomPackageRow(manifest, "search-engine", RowMode::ShowAll);
			++shown;
		}
		if (shown == 0) {
			ImGui::TextDisabled("No packages match the filter.");
		}

		ImGui::Unindent();
	}

	void PackageManagerPanel::HandleDiskInstall() {
		AxiomProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			m_StatusMessage = "Open a project before installing a package.";
			m_StatusIsError = true;
			return;
		}

		// Already in flight — ignore re-clicks.
		if (m_DiskInstallTask && m_DiskInstallTask->Running.load(std::memory_order_acquire)) {
			return;
		}

		// Join prior worker BEFORE clearing state — Running=false doesn't mean the lambda has unwound.
		if (m_DiskInstallWorker.joinable()) {
			m_DiskInstallWorker.join();
		}

		// Worker initializes STA, runs IFileOpenDialog::Show, signals Finished. Main polls in PollDiskInstallTask.
		{
			std::scoped_lock lock(m_DiskInstallTask->Mutex);
			m_DiskInstallTask->Finished = false;
			m_DiskInstallTask->PickedPath.clear();
		}
		m_DiskInstallTask->Running.store(true, std::memory_order_release);

		m_StatusMessage = "Choose a package folder...";
		m_StatusIsError = false;

		auto state = m_DiskInstallTask;
		m_DiskInstallWorker = std::thread([state]() {
			std::string picked;
#ifdef AIM_PLATFORM_WINDOWS
			HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			const bool ownsCom = SUCCEEDED(initResult);

			IFileOpenDialog* dialog = nullptr;
			HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
				IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog));

			if (SUCCEEDED(hr)) {
				DWORD options = 0;
				dialog->GetOptions(&options);
				dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
				dialog->SetTitle(L"Select an Axiom Package Folder");
				if (SUCCEEDED(dialog->Show(nullptr))) {
					IShellItem* item = nullptr;
					if (SUCCEEDED(dialog->GetResult(&item)) && item) {
						PWSTR widePath = nullptr;
						if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &widePath)) && widePath) {
							const int len = WideCharToMultiByte(CP_UTF8, 0, widePath, -1, nullptr, 0, nullptr, nullptr);
							if (len > 1) {
								std::string utf8(static_cast<size_t>(len - 1), '\0');
								WideCharToMultiByte(CP_UTF8, 0, widePath, -1, utf8.data(), len, nullptr, nullptr);
								picked = std::move(utf8);
							}
							CoTaskMemFree(widePath);
						}
						item->Release();
					}
				}
				dialog->Release();
			}

			if (ownsCom) CoUninitialize();
#endif

			{
				std::scoped_lock lock(state->Mutex);
				// Clear Running first, while still holding the mutex that
				// guards Finished/PickedPath. This way any observer that
				// sees Running==false is guaranteed to either (a) read
				// Finished==true on its next mutex-guarded poll, or
				// (b) be the next HandleDiskInstall caller, which now
				// joins this thread before mutating state.
				state->Running.store(false, std::memory_order_release);
				state->PickedPath = std::move(picked);
				state->Finished = true;
			}
		});
	}

	void PackageManagerPanel::PollDiskInstallTask() {
		if (!m_DiskInstallTask) return;

		bool finished = false;
		std::string picked;
		{
			std::scoped_lock lock(m_DiskInstallTask->Mutex);
			finished = m_DiskInstallTask->Finished;
			if (finished) picked = std::move(m_DiskInstallTask->PickedPath);
		}
		if (!finished) return;

		// Clear Finished so we don't re-process. Worker has already cleared Running.
		{
			std::scoped_lock lock(m_DiskInstallTask->Mutex);
			m_DiskInstallTask->Finished = false;
			m_DiskInstallTask->PickedPath.clear();
		}

		if (m_DiskInstallWorker.joinable()) {
			m_DiskInstallWorker.join();
		}

		// User cancelled — clear transient status.
		if (picked.empty()) {
			if (m_StatusMessage == "Choose a package folder...") {
				m_StatusMessage.clear();
			}
			return;
		}

		AxiomProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			m_StatusMessage = "Project closed before install completed.";
			m_StatusIsError = true;
			return;
		}

		auto result = AxiomPackageInstaller::InstallFromLocal(picked, project->PackagesDirectory);
		m_StatusMessage = result.Message;
		m_StatusIsError = !result.Success;
		if (!result.Success) return;

		if (!result.PackageName.empty()) {
			AxiomPackageInstaller::InstallToProject(*project, result.PackageName);
		}
		m_ManifestsDirty = true;
		StartPostInstallAutomation();
	}

	// ── New-Package wizard ──────────────────────────────────────────────────────
	// UI front-end over scripts/NewPackage.py. Uses --no-premake — post-install automation regens.
	void PackageManagerPanel::RenderNewPackageWindow() {
		if (!m_ShowNewPackageWindow) return;

		ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Create new package", &m_ShowNewPackageWindow,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) {

			ImGui::TextWrapped("Scaffolds a new Axiom package — manifest, source stubs, and "
				"(optionally) the project allow-list entry. Backed by scripts/NewPackage.py.");
			ImGui::Spacing();

			ImGui::TextUnformatted("Name");
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputTextWithHint("##NewPkgName", "e.g. Axiom.Foo or MyGame.Loot",
				m_NewPackageNameBuffer, sizeof(m_NewPackageNameBuffer));
			ImGui::TextDisabled("PascalCase segments separated by '.', e.g. Axiom.Tilemap2D.");

			ImGui::Spacing();
			ImGui::TextUnformatted("Description (optional)");
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputText("##NewPkgDesc", m_NewPackageDescriptionBuffer,
				sizeof(m_NewPackageDescriptionBuffer));

			ImGui::Spacing();
			ImGui::TextUnformatted("Layers");
			ImGui::Indent();
			ImGui::Checkbox("Native (engine_core)##NewPkgNative", &m_NewPackageLayerNative);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("C++ that links Axiom-Engine and can use AIM_*_TAG, ECS, etc.");
			}
			ImGui::Checkbox("Standalone C++ (no engine link)##NewPkgStandalone",
				&m_NewPackageLayerStandalone);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("C++ that does NOT link Axiom-Engine. Mutually exclusive with Native.");
			}
			ImGui::Checkbox("C# (.NET 9.0)##NewPkgCsharp", &m_NewPackageLayerCsharp);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Managed assembly (Pkg.<Name>.dll). Auto-bridges to the native sibling "
					"if Native or Standalone is also checked.");
			}
			ImGui::Unindent();

			// Mutual exclusion guardrail mirrors the CLI's parse_layers() check.
			if (m_NewPackageLayerNative && m_NewPackageLayerStandalone) {
				ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.20f, 1.0f),
					"Native and Standalone are mutually exclusive.");
			}

			ImGui::Spacing();
			ImGui::TextUnformatted("Target");
			ImGui::Indent();
			AxiomProject* project = ProjectManager::GetCurrentProject();
			ImGui::RadioButton("Engine packages (<repo>/packages/)##NewPkgEngine",
				&m_NewPackageTarget, 0);
			if (!project) ImGui::BeginDisabled();
			ImGui::RadioButton(project
				? "This project (<project>/Packages/)##NewPkgProject"
				: "This project (<no project loaded>)##NewPkgProject",
				&m_NewPackageTarget, 1);
			if (!project) ImGui::EndDisabled();
			ImGui::Unindent();

			if (!m_NewPackageError.empty()) {
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", m_NewPackageError.c_str());
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			const bool automating = m_AutomationTask && m_AutomationTask->Running.load(std::memory_order_acquire);
			const bool layerOk =
				(m_NewPackageLayerNative || m_NewPackageLayerStandalone || m_NewPackageLayerCsharp)
				&& !(m_NewPackageLayerNative && m_NewPackageLayerStandalone);
			const bool nameOk = std::strlen(m_NewPackageNameBuffer) > 0;
			const bool canCreate = layerOk && nameOk && !m_NewPackageIsCreating
				&& !m_IsOperating && !automating;

			if (!canCreate) ImGui::BeginDisabled();
			if (ImGui::Button("Create", ImVec2(120, 0))) {
				HandleNewPackageCreate();
			}
			if (!canCreate) ImGui::EndDisabled();

			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120, 0))) {
				m_ShowNewPackageWindow = false;
				m_NewPackageError.clear();
			}
		}
		ImGui::End();
	}

	void PackageManagerPanel::HandleNewPackageCreate() {
		m_NewPackageError.clear();

		const std::string engineRoot = AxiomProject::GetEngineRootDir();
		if (engineRoot.empty()) {
			m_NewPackageError = "Engine root not resolved; cannot locate scripts/NewPackage.py.";
			return;
		}

		const std::filesystem::path scriptPath =
			std::filesystem::path(engineRoot) / "scripts" / "NewPackage.py";
		if (!std::filesystem::exists(scriptPath)) {
			m_NewPackageError = "scripts/NewPackage.py not found at: " + scriptPath.generic_string();
			return;
		}

		// Comma separator — host shells don't re-parse it on either platform.
		std::vector<std::string> layerTokens;
		if (m_NewPackageLayerNative)     layerTokens.emplace_back("native");
		if (m_NewPackageLayerStandalone) layerTokens.emplace_back("standalone");
		if (m_NewPackageLayerCsharp)     layerTokens.emplace_back("csharp");
		std::string layersArg;
		for (size_t i = 0; i < layerTokens.size(); ++i) {
			if (i > 0) layersArg += ',';
			layersArg += layerTokens[i];
		}

		std::vector<std::string> command = {
			"python",
			scriptPath.generic_string(),
			std::string(m_NewPackageNameBuffer),
			"--layers", layersArg,
			"--no-premake",   // we run regen+build via StartPostInstallAutomation below
		};

		AxiomProject* project = ProjectManager::GetCurrentProject();
		if (m_NewPackageTarget == 1) {
			if (!project) {
				m_NewPackageError = "Project target selected but no project is loaded.";
				return;
			}
			command.emplace_back("--project");
			command.emplace_back(project->RootDirectory);
		}

		if (std::strlen(m_NewPackageDescriptionBuffer) > 0) {
			command.emplace_back("--description");
			command.emplace_back(m_NewPackageDescriptionBuffer);
		}

		m_NewPackageIsCreating = true;
		m_StatusMessage = std::string("Creating package '") + m_NewPackageNameBuffer + "'...";
		m_StatusIsError = false;

		// Synchronous: --no-premake makes this sub-second; one frame skip is fine.
		Process::Result result = Process::Run(command, std::filesystem::path(engineRoot));
		m_NewPackageIsCreating = false;

		if (!result.Succeeded()) {
			m_NewPackageError = "Scaffolder failed (exit " + std::to_string(result.ExitCode) +
				"). Output:\n" + result.Output;
			AIM_ERROR_TAG("AxiomPackages", "{}", m_NewPackageError);
			m_StatusMessage = "Package creation failed.";
			m_StatusIsError = true;
			return;
		}

		AIM_INFO_TAG("AxiomPackages", "Created package '{}'.", m_NewPackageNameBuffer);

		// Auto-regen+build only when a project is open — rebuilding the editor's own engine.dll is unsafe.
		m_ManifestsDirty = true;
		if (project) {
			StartPostInstallAutomation();
		}
		else {
			m_StatusMessage = std::string("Package '") + m_NewPackageNameBuffer +
				"' created. Run premake5 vs2022 + rebuild the engine solution to pick it up.";
			m_StatusIsError = false;
		}

		m_ShowNewPackageWindow = false;
		m_NewPackageNameBuffer[0] = '\0';
		m_NewPackageDescriptionBuffer[0] = '\0';
	}

	void PackageManagerPanel::RenderGitInstallWindow() {
		if (!m_ShowGitInstallWindow) return;

		ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Install package from git URL", &m_ShowGitInstallWindow,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) {

			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputTextWithHint("##GitUrl", "https://github.com/owner/repo.git",
				m_GitHubUrlBuffer, sizeof(m_GitHubUrlBuffer));
			ImGui::TextDisabled("The repository's root must contain axiom-package.lua.");
			ImGui::Spacing();

			AxiomProject* project = ProjectManager::GetCurrentProject();
			const bool automating = m_AutomationTask && m_AutomationTask->Running.load(std::memory_order_acquire);
			const bool canInstall = project && std::strlen(m_GitHubUrlBuffer) > 0 && !m_IsOperating && !automating;

			if (!canInstall) ImGui::BeginDisabled();
			if (ImGui::Button("Install", ImVec2(120, 0))) {
				const std::string url = m_GitHubUrlBuffer;
				auto result = AxiomPackageInstaller::InstallFromGitHub(url, project->PackagesDirectory);
				m_StatusMessage = result.Message;
				m_StatusIsError = !result.Success;
				if (result.Success) {
					if (!result.PackageName.empty()) {
						AxiomPackageInstaller::InstallToProject(*project, result.PackageName);
					}
					m_ManifestsDirty = true;
					m_GitHubUrlBuffer[0] = '\0';
					m_ShowGitInstallWindow = false;
					StartPostInstallAutomation();
				}
			}
			if (!canInstall) ImGui::EndDisabled();

			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120, 0))) {
				m_ShowGitInstallWindow = false;
			}
		}
		ImGui::End();
	}

	void PackageManagerPanel::RenderNuGetInstallWindow() {
		if (!m_ShowNuGetInstallWindow) return;

		ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Install package from NuGet", &m_ShowNuGetInstallWindow,
			ImGuiWindowFlags_NoDocking)) {

			if (!m_Manager || !m_Manager->IsReady()) {
				ImGui::TextDisabled("NuGet source unavailable.");
				ImGui::End();
				return;
			}

			m_SelectedSource = 0;

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
			bool enterPressed = ImGui::InputText("##NuGetSearch", m_NuGetSearchBuffer, sizeof(m_NuGetSearchBuffer),
				ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();

			const bool automating = m_AutomationTask && m_AutomationTask->Running.load(std::memory_order_acquire);
			bool canSearch = !m_IsSearching && !m_IsOperating && !automating && std::strlen(m_NuGetSearchBuffer) > 0;
			if (!canSearch) ImGui::BeginDisabled();
			if (ImGui::Button("Search", ImVec2(60, 0)) || (enterPressed && canSearch)) {
				TriggerNuGetSearch();
			}
			if (!canSearch) ImGui::EndDisabled();

			ImGui::Separator();
			ImGui::BeginChild("##NuGetResults", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
			if (m_IsSearching) {
				ImGui::TextDisabled("Searching...");
			}
			else if (m_SearchResults.empty() && !m_LastNuGetQuery.empty()) {
				ImGui::TextDisabled("No results found.");
			}
			else {
				for (int i = 0; i < static_cast<int>(m_SearchResults.size()); i++) {
					RenderNugetPackageRow(m_SearchResults[i], i);
				}
			}
			ImGui::EndChild();

			if (ImGui::Button("Close", ImVec2(120, 0))) {
				m_ShowNuGetInstallWindow = false;
			}
		}
		ImGui::End();
	}

	// ── Installed Packages tab ─────────────────────────────────────────────────────

	void PackageManagerPanel::RenderInstalledPackagesTab() {
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextWithHint("##InstalledFilter", "Filter installed packages...",
			m_InstalledFilterBuffer, sizeof(m_InstalledFilterBuffer));

		ImGui::Spacing();

		ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
		if (ImGui::CollapsingHeader("Axiom Packages##installed")) {
			RenderInstalledAxiomPackagesSection();
		}

		ImGui::Spacing();

		ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
		if (ImGui::CollapsingHeader("User Packages##installed")) {
			RenderInstalledUserPackagesSection();
		}

		ImGui::Spacing();

		ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
		if (ImGui::CollapsingHeader("NuGet Packages##installed")) {
			RenderInstalledNuGetPackagesSection();
		}
	}

	void PackageManagerPanel::RenderInstalledAxiomPackagesSection() {
		ImGui::Indent();

		AxiomProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			ImGui::TextDisabled("Open a project to see its installed engine packages.");
			ImGui::Unindent();
			return;
		}

		int shown = 0;
		for (const auto& manifest : m_AllManifests) {
			if (!manifest.IsEngine) continue;
			if (!IsPackageInstalled(manifest.Name)) continue;
			if (!MatchesFilter(manifest.Name, m_InstalledFilterBuffer)) continue;
			RenderAxiomPackageRow(manifest, "installed-engine", RowMode::InstalledOnly);
			++shown;
		}
		if (shown == 0) {
			ImGui::TextDisabled("No engine packages installed. Use the Search tab to install one.");
		}

		ImGui::Unindent();
	}

	void PackageManagerPanel::RenderInstalledUserPackagesSection() {
		ImGui::Indent();

		AxiomProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			ImGui::TextDisabled("Open a project to see its installed user packages.");
			ImGui::Unindent();
			return;
		}

		// Axiom packages from the allow-list; NuGet PackageReferences are in a separate panel.
		int shown = 0;
		for (const auto& manifest : m_AllManifests) {
			if (manifest.IsEngine) continue;
			if (!IsPackageInstalled(manifest.Name)) continue;
			if (!MatchesFilter(manifest.Name, m_InstalledFilterBuffer)) continue;
			RenderAxiomPackageRow(manifest, "installed-user", RowMode::InstalledOnly);
			++shown;
		}
		if (shown == 0) {
			ImGui::TextDisabled("No user packages installed. Add a package via the Search tab.");
		}

		ImGui::Unindent();
	}

	void PackageManagerPanel::RenderInstalledNuGetPackagesSection() {
		ImGui::Indent();

		AxiomProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			ImGui::TextDisabled("Open a project to see its installed NuGet packages.");
			ImGui::Unindent();
			return;
		}

		if (!m_Manager || !m_Manager->IsReady()) {
			ImGui::TextDisabled("NuGet source unavailable.");
			ImGui::Unindent();
			return;
		}

		if (m_InstalledNuGetDirty) {
			m_InstalledNuGetPackages = m_Manager->GetInstalledPackages();
			m_InstalledNuGetDirty = false;
		}

		int shown = 0;
		for (int i = 0; i < static_cast<int>(m_InstalledNuGetPackages.size()); i++) {
			const auto& pkg = m_InstalledNuGetPackages[i];
			if (!MatchesFilter(pkg.Id, m_InstalledFilterBuffer)) continue;
			RenderNugetPackageRow(pkg, i + 100000);
			++shown;
		}
		if (shown == 0) {
			ImGui::TextDisabled("No NuGet packages installed. Use the Search tab's '+' menu to install one.");
		}

		ImGui::Unindent();
	}

	// ── Shared helpers ─────────────────────────────────────────────────────────────

	bool PackageManagerPanel::IsPackageInstalled(const std::string& name) const {
		AxiomProject* project = ProjectManager::GetCurrentProject();
		if (!project) return false;
		return std::find(project->Packages.begin(), project->Packages.end(), name) != project->Packages.end();
	}

	void PackageManagerPanel::RenderLayerBadges(const AxiomPackageManifest& manifest) {
		// engine_core = green, standalone_cpp = blue, csharp = purple.
		auto chip = [](const char* label, ImVec4 color) {
			ImGui::PushStyleColor(ImGuiCol_Button, color);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 1));
			ImGui::SmallButton(label);
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(3);
		};

		bool any = false;
		if (manifest.HasNativeLayer) {
			if (any) ImGui::SameLine();
			chip("native", ImVec4(0.20f, 0.55f, 0.30f, 1.0f));
			any = true;
		}
		if (manifest.HasNativeStandaloneLayer) {
			if (any) ImGui::SameLine();
			chip("native_standalone", ImVec4(0.20f, 0.40f, 0.65f, 1.0f));
			any = true;
		}
		if (manifest.HasCSharpLayer) {
			if (any) ImGui::SameLine();
			chip("csharp", ImVec4(0.45f, 0.30f, 0.65f, 1.0f));
			any = true;
		}
		if (!any) {
			ImGui::TextDisabled("(no layers declared)");
		}
	}

	void PackageManagerPanel::RenderAxiomPackageRow(const AxiomPackageManifest& manifest, const char* idHint, RowMode mode) {
		const bool installed = IsPackageInstalled(manifest.Name);
		if (mode == RowMode::InstalledOnly && !installed) {
			return;
		}

		ImGui::PushID((std::string(idHint) + ":" + manifest.Name).c_str());

		ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "%s", manifest.Name.c_str());
		ImGui::SameLine();
		ImGui::TextDisabled("v%s", manifest.Version.c_str());

		AxiomProject* project = ProjectManager::GetCurrentProject();
		const float buttonWidth = 90.0f;
		const bool automating = m_AutomationTask && m_AutomationTask->Running.load(std::memory_order_acquire);
		const bool canMutate = (project != nullptr) && !m_IsOperating && !automating;

		if (installed) {
			ImGui::SameLine(ImGui::GetContentRegionMax().x - buttonWidth);
			if (!canMutate) ImGui::BeginDisabled();
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
			if (ImGui::Button("Uninstall", ImVec2(buttonWidth, 0))) {
				auto result = AxiomPackageInstaller::UninstallFromProject(*project, manifest.Name);
				m_StatusMessage = result.Message;
				m_StatusIsError = !result.Success;
				if (result.Success) {
					m_ManifestsDirty = true;
					StartPostInstallAutomation();
				}
			}
			ImGui::PopStyleColor();
			if (!canMutate) ImGui::EndDisabled();
		}
		else if (project) {
			ImGui::SameLine(ImGui::GetContentRegionMax().x - buttonWidth);
			if (!canMutate) ImGui::BeginDisabled();
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.2f, 1.0f));
			if (ImGui::Button("Install", ImVec2(buttonWidth, 0))) {
				auto result = AxiomPackageInstaller::InstallToProject(*project, manifest.Name);
				m_StatusMessage = result.Message;
				m_StatusIsError = !result.Success;
				if (result.Success) {
					m_ManifestsDirty = true;
					StartPostInstallAutomation();
				}
			}
			ImGui::PopStyleColor();
			if (!canMutate) ImGui::EndDisabled();
		}

		RenderLayerBadges(manifest);
		if (!manifest.Description.empty()) {
			ImGui::TextWrapped("%s", manifest.Description.c_str());
		}
		ImGui::Separator();
		ImGui::PopID();
	}

	void PackageManagerPanel::RenderNugetPackageRow(const PackageInfo& pkg, int index) {
		ImGui::PushID(index);

		if (pkg.Verified) {
			ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "%s", pkg.Id.c_str());
		}
		else {
			ImGui::TextUnformatted(pkg.Id.c_str());
		}
		ImGui::SameLine();
		ImGui::TextDisabled("v%s", pkg.Version.c_str());

		const float buttonWidth = 80.0f;
		ImGui::SameLine(ImGui::GetContentRegionMax().x - buttonWidth);

		bool disabled = m_IsOperating;
		if (disabled) ImGui::BeginDisabled();

		if (pkg.IsInstalled) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
			if (ImGui::Button("Remove", ImVec2(buttonWidth, 0))) {
				m_IsOperating = true;
				m_OperationWasInstall = false;
				m_OperationTarget = pkg.Id;
				m_OperationVersion.clear();
				m_StatusMessage = "Removing " + pkg.Id + "...";
				m_StatusIsError = false;
				m_OperationFuture = m_Manager->RemoveAsync(m_SelectedSource, pkg.Id);
			}
			ImGui::PopStyleColor();
		}
		else {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.2f, 1.0f));
			if (ImGui::Button("Install", ImVec2(buttonWidth, 0))) {
				m_IsOperating = true;
				m_OperationWasInstall = true;
				m_OperationTarget = pkg.Id;
				m_OperationVersion = pkg.Version;
				m_StatusMessage = "Installing " + pkg.Id + " " + pkg.Version + "...";
				m_StatusIsError = false;
				m_OperationFuture = m_Manager->InstallAsync(m_SelectedSource, pkg.Id, pkg.Version);
			}
			ImGui::PopStyleColor();
		}
		if (disabled) ImGui::EndDisabled();

		if (!pkg.Description.empty()) {
			ImGui::TextWrapped("%s", pkg.Description.c_str());
		}
		if (!pkg.Authors.empty() || pkg.TotalDownloads > 0) {
			ImGui::TextDisabled("%s", pkg.Authors.c_str());
			if (pkg.TotalDownloads > 0) {
				ImGui::SameLine();
				if (pkg.TotalDownloads >= 1000000)
					ImGui::TextDisabled("| %.1fM downloads", pkg.TotalDownloads / 1000000.0);
				else if (pkg.TotalDownloads >= 1000)
					ImGui::TextDisabled("| %.1fK downloads", pkg.TotalDownloads / 1000.0);
				else
					ImGui::TextDisabled("| %lld downloads", pkg.TotalDownloads);
			}
		}
		ImGui::Separator();
		ImGui::PopID();
	}

	void PackageManagerPanel::TriggerNuGetSearch() {
		m_LastNuGetQuery = m_NuGetSearchBuffer;
		m_IsSearching = true;
		m_StatusMessage = "Searching NuGet...";
		m_StatusIsError = false;
		m_SearchFuture = m_Manager->SearchAsync(m_SelectedSource, m_LastNuGetQuery, 20);
	}

	bool PackageManagerPanel::BrowseForLocalFolder(std::string& outPath) {
#ifdef AIM_PLATFORM_WINDOWS
		HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		const bool ownsCom = SUCCEEDED(initResult);

		IFileOpenDialog* dialog = nullptr;
		HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
			IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog));

		bool picked = false;
		if (SUCCEEDED(hr)) {
			DWORD options = 0;
			dialog->GetOptions(&options);
			dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
			dialog->SetTitle(L"Select an Axiom Package Folder");
			if (SUCCEEDED(dialog->Show(nullptr))) {
				IShellItem* item = nullptr;
				if (SUCCEEDED(dialog->GetResult(&item)) && item) {
					PWSTR widePath = nullptr;
					if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &widePath)) && widePath) {
						const int len = WideCharToMultiByte(CP_UTF8, 0, widePath, -1, nullptr, 0, nullptr, nullptr);
						if (len > 1) {
							std::string utf8(static_cast<size_t>(len - 1), '\0');
							WideCharToMultiByte(CP_UTF8, 0, widePath, -1, utf8.data(), len, nullptr, nullptr);
							outPath = std::move(utf8);
							picked = true;
						}
						CoTaskMemFree(widePath);
					}
					item->Release();
				}
			}
			dialog->Release();
		}

		if (ownsCom) CoUninitialize();
		return picked;
#else
		(void)outPath;
		return false;
#endif
	}

	void PackageManagerPanel::StartPostInstallAutomation() {
		AxiomProject* project = ProjectManager::GetCurrentProject();
		if (!project) return;
		if (!m_AutomationTask) return;
		if (m_AutomationTask->Running.load(std::memory_order_acquire)) return;

		// Snapshot the project root so the worker doesn't need to touch ProjectManager.
		const std::string projectRoot = project->RootDirectory;

		// Reset task state.
		{
			std::scoped_lock lock(m_AutomationTask->Mutex);
			m_AutomationTask->Stage = "Preparing...";
			m_AutomationTask->Progress = 0.05f;
			m_AutomationTask->Finished = false;
			m_AutomationTask->Success = false;
			m_AutomationTask->Error.clear();
		}
		m_AutomationTask->Running.store(true, std::memory_order_release);

		// Join any prior worker before spawning a new one.
		if (m_AutomationWorker.joinable()) {
			m_AutomationWorker.join();
		}

		// shared_ptr keeps state alive if panel shuts down mid-flight.
		auto state = m_AutomationTask;
		m_AutomationWorker = std::thread([state, projectRoot]() {
			auto setStage = [&state](const std::string& stage, float progress) {
				std::scoped_lock lock(state->Mutex);
				state->Stage = stage;
				state->Progress = progress;
			};

			setStage("Regenerating engine solution...", 0.10f);
			AxiomProject::RegenerateResult regen = AxiomProject::RegenerateSolutionForProject(projectRoot);
			const bool regenRanAndFailed = !regen.Succeeded && regen.ExitCode != -1;
			if (regenRanAndFailed) {
				std::scoped_lock lock(state->Mutex);
				state->Stage = "Solution regen failed";
				state->Progress = 1.0f;
				state->Success = false;
				state->Error = "Solution regeneration failed (premake exit code " +
					std::to_string(regen.ExitCode) + ").";
				state->Finished = true;
				state->Running.store(false, std::memory_order_release);
				return;
			}

			// Don't rebuild the engine — the editor has Axiom-Engine.dll loaded. Local packages only.
			const std::vector<std::string> packageNames =
				AxiomProject::EnumerateProjectLocalPackages(projectRoot);
			std::vector<std::string> targets;
			targets.reserve(packageNames.size() * 2);
			for (const std::string& pkg : packageNames) {
				targets.push_back("Pkg." + pkg + ".Native");
				targets.push_back("Pkg." + pkg);
			}

			setStage(targets.empty()
				? "No package projects to build; skipping MSBuild."
				: "Building project-local packages...", 0.40f);
			AxiomProject::BuildResult build = AxiomProject::BuildSolutionTargets(targets);
			{
				std::scoped_lock lock(state->Mutex);
				state->Progress = 1.0f;
				state->Success = build.Succeeded;
				if (!build.Succeeded) {
					state->Stage = "Build failed";
					state->Error = "MSBuild failed (exit code " + std::to_string(build.ExitCode) + ").";
				}
				else {
					state->Stage = "Done";
				}
				state->Finished = true;
			}
			state->Running.store(false, std::memory_order_release);
		});
	}

	void PackageManagerPanel::PollAutomationTask() {
		if (!m_AutomationTask) return;

		bool finished = false;
		bool success = false;
		std::string error;
		{
			std::scoped_lock lock(m_AutomationTask->Mutex);
			finished = m_AutomationTask->Finished;
			success = m_AutomationTask->Success;
			error = m_AutomationTask->Error;
		}

		if (!finished) return;

		// Race-protected: only the main thread mutates Finished back to false.
		{
			std::scoped_lock lock(m_AutomationTask->Mutex);
			m_AutomationTask->Finished = false;
		}

		if (m_AutomationWorker.joinable()) {
			m_AutomationWorker.join();
		}

		if (success) {
			// Hot-load new package DLLs so their components appear in the Add Component popup without a restart.
			const size_t newlyLoaded = PackageHost::LoadInstalled();
			if (newlyLoaded > 0) {
				m_StatusMessage = "Package operation complete; loaded " +
					std::to_string(newlyLoaded) + " new package(s) — components are available now.";
			}
			else {
				m_StatusMessage = "Package operation complete; engine solution rebuilt.";
			}
			m_StatusIsError = false;
		}
		else {
			m_StatusMessage = error.empty() ? "Package automation failed." : error;
			m_StatusIsError = true;
		}

		m_ManifestsDirty = true;
	}

}
