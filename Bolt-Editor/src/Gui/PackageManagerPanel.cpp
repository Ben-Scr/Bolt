#include <pch.hpp>
#include "Gui/PackageManagerPanel.hpp"
#include "Project/ProjectManager.hpp"
#include "Scripting/ScriptEngine.hpp"

#include <imgui.h>
#include <chrono>

namespace Bolt {

	void PackageManagerPanel::Initialize(PackageManager* manager) {
		m_Manager = manager;
	}

	void PackageManagerPanel::Shutdown() {
		m_Manager = nullptr;
		m_SearchResults.clear();
		m_InstalledPackages.clear();
	}

	void PackageManagerPanel::Render() {
		if (!m_Manager || !m_Manager->IsReady()) {
			ImGui::TextDisabled("Package manager not available");
			if (!m_Manager)
				ImGui::TextWrapped("No package manager instance.");
			else
				ImGui::TextWrapped("Bolt-PackageTool could not be resolved or built for the current platform.");
			return;
		}

		// Poll async search
		if (m_IsSearching && m_SearchFuture.valid()) {
			if (m_SearchFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
				m_SearchResults = m_SearchFuture.get();
				m_IsSearching = false;
				m_StatusMessage = std::to_string(m_SearchResults.size()) + " results";
				m_StatusIsError = false;
			}
		}

		// Poll async operation (install/remove)
		if (m_IsOperating && m_OperationFuture.valid()) {
			if (m_OperationFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
				auto result = m_OperationFuture.get();
				m_IsOperating = false;
				m_StatusMessage = result.Message;
				m_StatusIsError = !result.Success;
				m_InstalledDirty = true;

				// Update search results in-place so the button flips immediately
				if (result.Success) {
					for (auto& pkg : m_SearchResults) {
						if (pkg.Id == m_OperationTarget) {
							pkg.IsInstalled = m_OperationWasInstall;
							if (m_OperationWasInstall)
								pkg.InstalledVersion = m_OperationVersion;
							else
								pkg.InstalledVersion.clear();
							break;
						}
					}
				}

				// Trigger assembly reload on main thread
				if (m_Manager->NeedsReload()) {
					if (ScriptEngine::IsInitialized())
						ScriptEngine::ReloadAssemblies();
					m_Manager->ClearReloadFlag();
				}
			}
		}

		// Tabs
		if (ImGui::BeginTabBar("##PackageTabs")) {
			if (ImGui::BeginTabItem("Browse")) {
				RenderBrowseTab();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Installed")) {
				RenderInstalledTab();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		RenderStatusBar();
	}

	void PackageManagerPanel::RenderBrowseTab() {
		// Source selector
		const auto& sources = m_Manager->GetSources();
		if (!sources.empty()) {
			ImGui::SetNextItemWidth(150.0f);
			if (ImGui::BeginCombo("##Source", sources[m_SelectedSource]->GetName().c_str())) {
				for (int i = 0; i < static_cast<int>(sources.size()); i++) {
					bool selected = (m_SelectedSource == i);
					if (ImGui::Selectable(sources[i]->GetName().c_str(), selected)) {
						m_SelectedSource = i;
						m_SearchResults.clear();
					}
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
		}

		// Search bar
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
		bool enterPressed = ImGui::InputText("##Search", m_SearchBuffer, sizeof(m_SearchBuffer),
			ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();

		bool canSearch = !m_IsSearching && !m_IsOperating && strlen(m_SearchBuffer) > 0;
		if (!canSearch) ImGui::BeginDisabled();
		if (ImGui::Button("Search", ImVec2(60, 0)) || (enterPressed && canSearch)) {
			TriggerSearch();
		}
		if (!canSearch) ImGui::EndDisabled();

		ImGui::Separator();

		// Results list
		ImGui::BeginChild("##SearchResults", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4));

		if (m_IsSearching) {
			ImGui::TextDisabled("Searching...");
		}
		else if (m_SearchResults.empty() && !m_LastQuery.empty()) {
			ImGui::TextDisabled("No results found");
		}
		else {
			for (int i = 0; i < static_cast<int>(m_SearchResults.size()); i++) {
				RenderPackageRow(m_SearchResults[i], i);
			}
		}

		ImGui::EndChild();
	}

	void PackageManagerPanel::RenderInstalledTab() {
		if (m_InstalledDirty) {
			m_InstalledPackages = m_Manager->GetInstalledPackages();
			m_InstalledDirty = false;
		}

		ImGui::BeginChild("##InstalledList", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4));

		if (m_InstalledPackages.empty()) {
			ImGui::TextDisabled("No packages installed");
		}
		else {
			for (int i = 0; i < static_cast<int>(m_InstalledPackages.size()); i++) {
				RenderPackageRow(m_InstalledPackages[i], i + 10000);
			}
		}

		ImGui::EndChild();
	}

	void PackageManagerPanel::RenderPackageRow(const PackageInfo& pkg, int index) {
		ImGui::PushID(index);

		// Package name + version
		if (pkg.Verified) {
			ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "%s", pkg.Id.c_str());
		}
		else {
			ImGui::TextUnformatted(pkg.Id.c_str());
		}

		ImGui::SameLine();
		ImGui::TextDisabled("v%s", pkg.Version.c_str());

		// Action button (right-aligned)
		float buttonWidth = 70.0f;
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

		// Description
		if (!pkg.Description.empty()) {
			ImGui::TextWrapped("%s", pkg.Description.c_str());
		}

		// Authors + downloads
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

	void PackageManagerPanel::RenderStatusBar() {
		if (m_IsOperating) {
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", m_StatusMessage.c_str());
		}
		else if (m_StatusIsError) {
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", m_StatusMessage.c_str());
		}
		else if (!m_StatusMessage.empty()) {
			ImGui::TextDisabled("%s", m_StatusMessage.c_str());
		}
	}

	void PackageManagerPanel::TriggerSearch() {
		m_LastQuery = m_SearchBuffer;
		m_IsSearching = true;
		m_StatusMessage = "Searching...";
		m_StatusIsError = false;
		m_SearchFuture = m_Manager->SearchAsync(m_SelectedSource, m_LastQuery, 20);
	}

}
