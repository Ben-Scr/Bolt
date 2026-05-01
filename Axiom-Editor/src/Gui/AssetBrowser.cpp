#include <pch.hpp>
#include "Assets/AssetRegistry.hpp"
#include "Gui/AssetBrowser.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Serialization/Path.hpp"
#include "Project/ProjectManager.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/Scene.hpp"
#include "Components/General/NameComponent.hpp"
#include "Core/Log.hpp"
#include "Editor/ExternalEditor.hpp"
#include <imgui.h>
#include <algorithm>
#include "Gui/EditorIcons.hpp"
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>

#include <thread>

#ifdef AIM_PLATFORM_WINDOWS
#include <windows.h>
#include <shellapi.h>
#endif

namespace Axiom {
	namespace {
		bool IsLeftMouseDragPastClickThreshold()
		{
			const ImGuiIO& io = ImGui::GetIO();
			const ImVec2 delta(io.MousePos.x - io.MouseClickedPos[ImGuiMouseButton_Left].x,
				io.MousePos.y - io.MouseClickedPos[ImGuiMouseButton_Left].y);
			return (delta.x * delta.x + delta.y * delta.y) > (io.MouseDragThreshold * io.MouseDragThreshold);
		}

		std::string GetDuplicateBaseName(const std::string& stem)
		{
			if (stem.size() < 4 || stem.back() != ')') {
				return stem;
			}

			const std::size_t open = stem.rfind(" (");
			if (open == std::string::npos || open + 2 >= stem.size() - 1) {
				return stem;
			}

			for (std::size_t i = open + 2; i < stem.size() - 1; ++i) {
				if (!std::isdigit(static_cast<unsigned char>(stem[i]))) {
					return stem;
				}
			}

			return stem.substr(0, open);
		}

		std::filesystem::path MakeUniqueAssetPath(
			const std::filesystem::path& source,
			const std::filesystem::path& destinationDirectory,
			bool preserveOriginalNameWhenFree)
		{
			std::error_code ec;
			std::filesystem::path candidate = destinationDirectory / source.filename();
			if (preserveOriginalNameWhenFree && !std::filesystem::exists(candidate, ec)) {
				return candidate;
			}

			const std::string stem = GetDuplicateBaseName(source.stem().string());
			const std::string extension = source.extension().string();
			for (int counter = 1; counter < 10000; ++counter) {
				candidate = destinationDirectory / (stem + " (" + std::to_string(counter) + ")" + extension);
				ec.clear();
				if (!std::filesystem::exists(candidate, ec)) {
					return candidate;
				}
			}

			return destinationDirectory / (stem + " (" + std::to_string(std::time(nullptr)) + ")" + extension);
		}

		bool CopyEntryTo(const std::filesystem::path& source, const std::filesystem::path& destination)
		{
			std::error_code ec;
			if (!std::filesystem::exists(source, ec) || ec) {
				return false;
			}

			if (destination.has_parent_path()) {
				std::filesystem::create_directories(destination.parent_path(), ec);
				if (ec) {
					return false;
				}
			}

			if (std::filesystem::is_directory(source, ec) && !ec) {
				std::filesystem::copy(source, destination, std::filesystem::copy_options::recursive, ec);
				return !ec;
			}

			ec.clear();
			std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, ec);
			return !ec;
		}

		bool MoveEntryTo(const std::filesystem::path& source, const std::filesystem::path& destination)
		{
			std::error_code ec;
			if (!std::filesystem::exists(source, ec) || ec) {
				return false;
			}

			if (destination.has_parent_path()) {
				std::filesystem::create_directories(destination.parent_path(), ec);
				if (ec) {
					return false;
				}
			}

			std::filesystem::rename(source, destination, ec);
			if (!ec) {
				if (std::filesystem::is_regular_file(destination, ec)) {
					AssetRegistry::MoveCompanionMetadata(source.string(), destination.string());
				}
				return true;
			}

			ec.clear();
			if (!CopyEntryTo(source, destination)) {
				return false;
			}

			Directory::Delete(source.string());
			return true;
		}
	}

	static const char* GetFileTypeIconName(const std::string& extension) {
		std::string ext = extension;
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

		if (ext == ".cs")                                                    return "asset_cs";
		if (ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp")    return "asset_cpp";
		if (ext == ".scene" || ext == ".axiom")                               return "asset_scene";
		if (ext == ".prefab")                                                return "asset_scene";
		if (ext == ".txt" || ext == ".cfg" || ext == ".ini" ||
			ext == ".yaml" || ext == ".toml" || ext == ".xml" ||
			ext == ".json" || ext == ".lua")                                 return "asset_txt";
		if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") return "asset_audio";

		return nullptr;
	}

	void AssetBrowser::Initialize(const std::string& rootDirectory) {
		m_RootDirectory = rootDirectory;
		m_CurrentDirectory = rootDirectory;
		m_Thumbnails.Initialize();
		m_NeedsRefresh = true;
	}

	void AssetBrowser::Shutdown() {
		m_Thumbnails.Shutdown();
	}

	void AssetBrowser::NavigateTo(const std::string& directory) {
		m_CurrentDirectory = directory;
		ClearAssetSelection();
		CancelRename();
		m_NeedsRefresh = true;
	}

	void AssetBrowser::NavigateUp() {
		std::filesystem::path current(m_CurrentDirectory);
		std::filesystem::path root(m_RootDirectory);

		if (current != root && current.has_parent_path()) {
			std::filesystem::path parent = current.parent_path();
			if (parent.string().size() >= root.string().size()) {
				NavigateTo(parent.string());
			}
		}
	}

	void AssetBrowser::Refresh() {
		AssetRegistry::Sync();
		m_Entries = Directory::GetEntries(m_CurrentDirectory);
		m_NeedsRefresh = false;
	}

	void AssetBrowser::ClearAssetSelection() {
		m_SelectedPath.clear();
		m_SelectedPaths.clear();
		m_LastSelectionIndex = -1;
		m_PressedPath.clear();
		m_SelectionActivated = false;
		CancelRename();
	}

	bool AssetBrowser::IsPathSelected(const std::string& path) const {
		if (m_SelectedPaths.empty()) {
			return m_SelectedPath == path;
		}
		return std::find(m_SelectedPaths.begin(), m_SelectedPaths.end(), path) != m_SelectedPaths.end();
	}

	std::vector<std::string> AssetBrowser::GetSelectedPaths() const {
		if (!m_SelectedPaths.empty()) {
			return m_SelectedPaths;
		}

		if (!m_SelectedPath.empty()) {
			return { m_SelectedPath };
		}

		return {};
	}

	void AssetBrowser::SetSingleSelection(const std::string& path, int index) {
		m_SelectedPath = path;
		m_SelectedPaths = { path };
		m_LastSelectionIndex = index;
	}

	void AssetBrowser::ToggleSelection(const std::string& path, int index) {
		auto it = std::find(m_SelectedPaths.begin(), m_SelectedPaths.end(), path);
		if (it != m_SelectedPaths.end()) {
			m_SelectedPaths.erase(it);
			if (m_SelectedPath == path) {
				m_SelectedPath = m_SelectedPaths.empty() ? std::string() : m_SelectedPaths.back();
			}
		}
		else {
			m_SelectedPaths.push_back(path);
			m_SelectedPath = path;
		}

		m_LastSelectionIndex = index;
	}

	void AssetBrowser::SelectRange(int index) {
		if (m_VisibleEntryPaths.empty()) {
			return;
		}

		if (m_LastSelectionIndex < 0 || m_LastSelectionIndex >= static_cast<int>(m_VisibleEntryPaths.size())) {
			SetSingleSelection(m_VisibleEntryPaths[static_cast<std::size_t>(index)], index);
			return;
		}

		const int first = std::min(m_LastSelectionIndex, index);
		const int last = std::max(m_LastSelectionIndex, index);
		m_SelectedPaths.clear();
		for (int i = first; i <= last; ++i) {
			m_SelectedPaths.push_back(m_VisibleEntryPaths[static_cast<std::size_t>(i)]);
		}
		m_SelectedPath = m_VisibleEntryPaths[static_cast<std::size_t>(index)];
	}

	void AssetBrowser::HandleAssetShortcuts() {
		if (m_IsRenaming || ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive()) {
			return;
		}

		const ImGuiIO& io = ImGui::GetIO();
		if (!io.KeyCtrl) {
			return;
		}

		if (ImGui::IsKeyPressed(ImGuiKey_X)) {
			CopySelectedAssets(true);
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_C)) {
			CopySelectedAssets(false);
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_V)) {
			PasteAssets();
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_D)) {
			DuplicateSelectedAssets();
		}
	}

	void AssetBrowser::CopySelectedAssets(bool cut) {
		m_AssetClipboardPaths = GetSelectedPaths();
		m_AssetClipboardCut = cut && !m_AssetClipboardPaths.empty();
	}

	void AssetBrowser::PasteAssets() {
		if (m_AssetClipboardPaths.empty()) {
			return;
		}

		std::vector<std::string> pastedPaths;
		const std::filesystem::path targetDirectory(m_CurrentDirectory);

		for (const std::string& sourcePathString : m_AssetClipboardPaths) {
			const std::filesystem::path sourcePath(sourcePathString);
			std::error_code ec;
			if (!std::filesystem::exists(sourcePath, ec) || ec) {
				continue;
			}

			const bool sameDirectory = std::filesystem::equivalent(sourcePath.parent_path(), targetDirectory, ec) && !ec;
			const std::filesystem::path targetPath = MakeUniqueAssetPath(sourcePath, targetDirectory, !sameDirectory);

			bool succeeded = false;
			if (m_AssetClipboardCut) {
				if (sameDirectory) {
					continue;
				}
				succeeded = MoveEntryTo(sourcePath, targetPath);
			}
			else {
				succeeded = CopyEntryTo(sourcePath, targetPath);
			}

			if (succeeded) {
				pastedPaths.push_back(targetPath.string());
				m_Thumbnails.Invalidate(sourcePath.string());
				m_Thumbnails.Invalidate(targetPath.string());
			}
		}

		if (m_AssetClipboardCut) {
			m_AssetClipboardPaths.clear();
			m_AssetClipboardCut = false;
		}

		if (!pastedPaths.empty()) {
			m_SelectedPaths = pastedPaths;
			m_SelectedPath = pastedPaths.back();
			m_LastSelectionIndex = -1;
			m_NeedsRefresh = true;
		}
	}

	void AssetBrowser::DuplicateSelectedAssets() {
		const std::vector<std::string> selectedPaths = GetSelectedPaths();
		if (selectedPaths.empty()) {
			return;
		}

		std::vector<std::string> duplicatedPaths;
		for (const std::string& sourcePathString : selectedPaths) {
			const std::filesystem::path sourcePath(sourcePathString);
			std::error_code ec;
			if (!std::filesystem::exists(sourcePath, ec) || ec) {
				continue;
			}

			const std::filesystem::path targetPath = MakeUniqueAssetPath(sourcePath, sourcePath.parent_path(), false);
			if (CopyEntryTo(sourcePath, targetPath)) {
				duplicatedPaths.push_back(targetPath.string());
				m_Thumbnails.Invalidate(targetPath.string());
			}
		}

		if (!duplicatedPaths.empty()) {
			m_SelectedPaths = duplicatedPaths;
			m_SelectedPath = duplicatedPaths.back();
			m_LastSelectionIndex = -1;
			m_NeedsRefresh = true;
		}
	}

	void AssetBrowser::Render() {
		m_SelectionActivated = false;

		// Process pending OS file drops
		if (!m_PendingExternalDrops.empty()) {
			OnExternalFileDrop(m_PendingExternalDrops);
			m_PendingExternalDrops.clear();
		}

		// Load pending scene on main thread (before ImGui frame)
		if (!m_PendingSceneLoad.empty())
		{
			std::string scenePath = m_PendingSceneLoad;
			m_PendingSceneLoad.clear();

			Scene* active = SceneManager::Get().GetActiveScene();
			if (active)
			{
				SceneSerializer::LoadFromFile(*active, scenePath);

				AxiomProject* project = ProjectManager::GetCurrentProject();
				if (project)
				{
					std::string sceneName = std::filesystem::path(scenePath).stem().string();
					project->LastOpenedScene = sceneName;
					project->Save();
				}
			}
		}

		ImGui::Begin("Project");

		if (m_NeedsRefresh) {
			Refresh();
		}

		RenderBreadcrumb();
		ImGui::Separator();
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
			&& ImGui::IsKeyPressed(ImGuiKey_F2)
			&& !ImGui::GetIO().WantTextInput
			&& !ImGui::IsAnyItemActive()) {
			BeginRenameSelected();
		}
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
			HandleAssetShortcuts();
		}
		RenderGrid();

		ImGui::End();
	}

	void AssetBrowser::RenderBreadcrumb() {
		std::filesystem::path root(m_RootDirectory);
		std::filesystem::path current(m_CurrentDirectory);
		std::filesystem::path relative = std::filesystem::relative(current, root);

		if (m_CurrentDirectory != m_RootDirectory) {
			if (ImGui::SmallButton("<")) {
				NavigateUp();
				return;
			}
			ImGui::SameLine();
		}

		if (ImGui::SmallButton("Assets")) {
			NavigateTo(m_RootDirectory);
			return;
		}

		if (relative != "." && !relative.empty()) {
			std::filesystem::path accumulated = root;
			for (const auto& segment : relative) {
				accumulated /= segment;
				ImGui::SameLine();
				ImGui::TextUnformatted("/");
				ImGui::SameLine();

				std::string segStr = segment.string();
				std::string accStr = accumulated.string();
				bool truncated = false;
				const std::string displaySegment = ImGuiUtils::Ellipsize(segStr, 140.0f, &truncated);

				if (accStr == m_CurrentDirectory) {
					ImGui::TextUnformatted(displaySegment.c_str());
					if (truncated && ImGui::IsItemHovered()) {
						ImGui::SetTooltip("%s", segStr.c_str());
					}
				}
				else {
					if (ImGui::SmallButton((displaySegment + "##" + accStr).c_str())) {
						NavigateTo(accStr);
						return;
					}
					if (truncated && ImGui::IsItemHovered()) {
						ImGui::SetTooltip("%s", segStr.c_str());
					}
				}
			}
		}

		ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 30.0f);
		{
			unsigned int refreshIcon = EditorIcons::Get("redo", 16);
			bool clicked = false;
			if (refreshIcon) {
				clicked = ImGui::ImageButton("##Refresh",
					static_cast<ImTextureID>(static_cast<intptr_t>(refreshIcon)),
					ImVec2(14, 14), ImVec2(0, 1), ImVec2(1, 0));
			} else {
				clicked = ImGui::SmallButton("R");
			}
			if (clicked) m_NeedsRefresh = true;
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Refresh");
		}
	}

	void AssetBrowser::RenderGrid() {
		ImGui::BeginChild("AssetGrid", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_None);

		const float cellSize = m_TileSize + m_TilePadding;
		const float panelWidth = ImGui::GetContentRegionAvail().x;
		int columns = static_cast<int>(panelWidth / cellSize);
		if (columns < 1) columns = 1;

		m_ItemRightClicked = false;

		if (ImGui::IsWindowHovered()
			&& ImGui::IsMouseReleased(ImGuiMouseButton_Left)
			&& !IsLeftMouseDragPastClickThreshold()
			&& !ImGui::IsAnyItemHovered()) {
			ClearAssetSelection();
		}

		std::vector<DirectoryEntry> visibleEntries = m_Entries;
		if (m_PendingScriptType != PendingScriptType::None
			&& m_PendingScriptDir == m_CurrentDirectory
			&& !m_RenamePath.empty()) {
			DirectoryEntry pendingEntry;
			pendingEntry.Path = m_RenamePath;
			pendingEntry.Name = std::filesystem::path(m_RenamePath).filename().string();
			pendingEntry.IsDirectory = false;
			visibleEntries.push_back(std::move(pendingEntry));
		}

		m_VisibleEntryPaths.clear();
		m_VisibleEntryPaths.reserve(visibleEntries.size());
		for (const DirectoryEntry& entry : visibleEntries) {
			m_VisibleEntryPaths.push_back(entry.Path);
		}

		for (int i = 0; i < static_cast<int>(visibleEntries.size()); i++) {
			if (i % columns != 0) {
				ImGui::SameLine();
			}
			RenderAssetTile(visibleEntries[i], i);
		}

		if (visibleEntries.empty()) {
			ImGui::TextDisabled("Empty folder");
		}

		RenderGridContextMenu();

		// Accept entity drops from hierarchy panel to save as prefab
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
				struct HierarchyDragData { int Index; uint32_t EntityHandle; };
				auto* dragData = static_cast<const HierarchyDragData*>(payload->Data);
				entt::entity entityHandle = static_cast<entt::entity>(dragData->EntityHandle);

				Scene* activeScene = SceneManager::Get().GetActiveScene();
				if (activeScene && activeScene->IsValid(entityHandle)) {
					std::string entityName = "Entity";
					if (activeScene->HasComponent<NameComponent>(entityHandle))
						entityName = activeScene->GetComponent<NameComponent>(entityHandle).Name;
					std::string prefabPath = (std::filesystem::path(m_CurrentDirectory) / (entityName + ".prefab")).string();
					SceneSerializer::SaveEntityToFile(*activeScene, entityHandle, prefabPath);
					m_NeedsRefresh = true;
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::EndChild();
	}


	void AssetBrowser::RenderAssetTile(const DirectoryEntry& entry, int index) {
		ImGui::PushID(index);

		const bool isSelected = IsPathSelected(entry.Path);

		ImGui::BeginGroup();

		ImVec2 cursorPos = ImGui::GetCursorScreenPos();

		if (isSelected) {
			ImVec2 bgMin(cursorPos.x - 2, cursorPos.y - 2);
			ImVec2 bgMax(cursorPos.x + m_TileSize + 2, cursorPos.y + m_TileSize + ImGui::GetTextLineHeightWithSpacing() + 2);
			ImGui::GetWindowDrawList()->AddRectFilled(bgMin, bgMax, IM_COL32(60, 100, 160, 180), 4.0f);
		}

		ImVec2 iconPos = cursorPos;
		unsigned int thumbnail = 0;

		if (!entry.IsDirectory) {
			thumbnail = m_Thumbnails.GetThumbnail(entry.Path);
		}

		if (thumbnail != 0) {
			auto it = m_Thumbnails.GetCacheEntry(entry.Path);
			float texW = m_TileSize, texH = m_TileSize;
			if (it) {
				texW = it->GetWidth();
				texH = it->GetHeight();
			}

			float drawW = m_TileSize;
			float drawH = m_TileSize;

			if (texW > 0.0f && texH > 0.0f) {
				float aspect = texW / texH;
				if (aspect > 1.0f) {
					drawH = m_TileSize / aspect;
				}
				else {
					drawW = m_TileSize * aspect;
				}
			}

			float offsetX = (m_TileSize - drawW) * 0.5f;
			float offsetY = (m_TileSize - drawH) * 0.5f;

			ImGui::SetCursorScreenPos(ImVec2(iconPos.x + offsetX, iconPos.y + offsetY));
			ImGui::Image(
				static_cast<ImTextureID>(static_cast<intptr_t>(thumbnail)),
				ImVec2(drawW, drawH),
				ImVec2(0, 1), ImVec2(1, 0)
			);

			ImGui::SetCursorScreenPos(ImVec2(iconPos.x, iconPos.y + m_TileSize));
		}
		else {
			AssetType type = entry.IsDirectory
				? AssetType::Folder
				: ThumbnailCache::GetAssetType(std::filesystem::path(entry.Path).extension().string());

			bool drewTexture = false;
			const char* iconName = nullptr;

			if (type == AssetType::Folder) {
				iconName = "open_folder";
			}
			else if (!entry.IsDirectory) {
				iconName = GetFileTypeIconName(std::filesystem::path(entry.Path).extension().string());
				if (!iconName) iconName = "asset_bin";
			}

			if (iconName) {
				unsigned int icon = EditorIcons::Get(iconName, (int)m_TileSize);
				if (icon) {
					const float pad = m_TileSize * 0.1f;
					const float drawSize = m_TileSize - pad * 2.0f;
					ImGui::SetCursorScreenPos(ImVec2(iconPos.x + pad, iconPos.y + pad));
					ImGui::Image(
						static_cast<ImTextureID>(static_cast<intptr_t>(icon)),
						ImVec2(drawSize, drawSize),
						ImVec2(0, 1), ImVec2(1, 0)
					);
					ImGui::SetCursorScreenPos(ImVec2(iconPos.x, iconPos.y + m_TileSize));
					drewTexture = true;
				}
			}

			if (!drewTexture) {
				ThumbnailCache::DrawAssetIcon(type, iconPos, m_TileSize);
				ImGui::Dummy(ImVec2(m_TileSize, m_TileSize));
			}
		}

		if (IsRenamingEntry(entry.Path)) {
			m_RenameFrameCounter++;

			ImGui::PushItemWidth(m_TileSize);

			if (m_RenameFrameCounter == 1) {
				ImGui::SetKeyboardFocusHere();
			}

			bool committed = ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer),
				ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

			if (committed) {
				CommitRename();
			}
			else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				CancelRename();
			}
			else if (m_RenameFrameCounter > 2 && !ImGui::IsItemActive()) {
				CommitRename();
			}

			ImGui::PopItemWidth();
		}
		else {
			const float maxWidth = m_TileSize;
			const std::string& name = entry.Name;
			bool truncated = false;
			const std::string display = ImGuiUtils::Ellipsize(name, maxWidth, &truncated);
			const float textWidth = ImGui::CalcTextSize(display.c_str()).x;
			const float offsetX = (maxWidth - textWidth) * 0.5f;
			if (offsetX > 0.0f) {
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
			}
			ImGui::TextUnformatted(display.c_str());
			if (truncated && ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", name.c_str());
			}
		}

		ImGui::EndGroup();

		if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			m_PressedPath = entry.Path;
		}

		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			if (ImGui::IsItemHovered() && m_PressedPath == entry.Path && !IsLeftMouseDragPastClickThreshold()) {
				const ImGuiIO& io = ImGui::GetIO();
				if (io.KeyShift) {
					SelectRange(index);
				}
				else if (io.KeyCtrl) {
					ToggleSelection(entry.Path, index);
				}
				else {
					SetSingleSelection(entry.Path, index);
				}
				m_SelectionActivated = true;
				if (!IsRenamingEntry(entry.Path)) {
					CancelRename();
				}
			}
			if (m_PressedPath == entry.Path) {
				m_PressedPath.clear();
			}
		}

		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			if (entry.IsDirectory) {
				NavigateTo(entry.Path);  // deferred via m_NeedsRefresh
			} else {
				OpenAssetExternal(entry);  // deferred via m_DeferredOpenPath
			}
		}

		RenderItemContextMenu(entry, index);

		HandleDragSource(entry);
		if (entry.IsDirectory) {
			HandleDropTarget(entry);
		}

		ImGui::PopID();

		ImGui::SameLine(0, 0);
		ImGui::Dummy(ImVec2(m_TilePadding, 0));
	}

	void AssetBrowser::OpenAssetPath(const std::string& path) {
		std::error_code ec;
		if (!std::filesystem::exists(path, ec) || ec) {
			return;
		}

		if (std::filesystem::is_directory(path, ec) && !ec) {
			NavigateTo(path);
			return;
		}

		DirectoryEntry entry;
		entry.Path = path;
		entry.Name = std::filesystem::path(path).filename().string();
		entry.IsDirectory = false;
		OpenAssetExternal(entry);
	}

	void AssetBrowser::RevealAssetInExplorer(const std::string& path) {
#ifdef AIM_PLATFORM_WINDOWS
		std::string args = "/select,\"" + path + "\"";
		ShellExecuteA(nullptr, "open", "explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
#else
		std::filesystem::path target(path);
		DirectoryEntry entry;
		entry.Path = target.has_parent_path() ? target.parent_path().string() : path;
		entry.Name = std::filesystem::path(entry.Path).filename().string();
		entry.IsDirectory = true;
		OpenAssetExternal(entry);
#endif
	}


	void AssetBrowser::RenderGridContextMenu() {
		if (!m_ItemRightClicked &&
			ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
			ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
		{
			ImGui::OpenPopup("##AssetGridCtx");
		}

		if (ImGui::BeginPopup("##AssetGridCtx")) {
			if (!m_AssetClipboardPaths.empty()) {
				if (ImGui::MenuItem("Paste Asset", "Ctrl+V")) {
					PasteAssets();
				}
				ImGui::Separator();
			}

			if (ImGui::BeginMenu("Create")) {
				if (ImGui::MenuItem("EntityScript (C#)")) {
					CreateScript(m_CurrentDirectory);
				}
				if (ImGui::MenuItem("Component (C#)")) {
					CreateCSharpComponent(m_CurrentDirectory);
				}
				if (ImGui::MenuItem("GameSystem (C#)")) {
					CreateGameSystem(m_CurrentDirectory);
				}
				if (ImGui::MenuItem("GlobalSystem (C#)")) {
					CreateGlobalSystem(m_CurrentDirectory);
				}
				if (ImGui::MenuItem("Component (C++)")) {
					CreateNativeComponent(m_CurrentDirectory);
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Scene")) {
					CreateScene(m_CurrentDirectory);
				}
				if (ImGui::MenuItem("Entity")) {
					if (Scene* scene = SceneManager::Get().GetActiveScene()) {
						scene->CreateEntity("Entity");
						scene->MarkDirty();
					}
				}
				if (ImGui::MenuItem("Folder")) {
					CreateFolder(m_CurrentDirectory);
				}
				ImGui::EndMenu();
			}

			ImGui::EndPopup();
		}
	}

	void AssetBrowser::RenderItemContextMenu(const DirectoryEntry& entry, int index) {
		if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
			ImGui::OpenPopup("##ItemCtx");
			m_ItemRightClicked = true;
			if (!IsPathSelected(entry.Path)) {
				SetSingleSelection(entry.Path, index);
			}
		}

		if (ImGui::BeginPopup("##ItemCtx")) {
			if (!IsPathSelected(entry.Path)) {
				SetSingleSelection(entry.Path, index);
			}

			if (ImGui::MenuItem("Copy Asset", "Ctrl+C")) {
				CopySelectedAssets(false);
			}
			if (ImGui::MenuItem("Open Asset")) {
				OpenAssetPath(entry.Path);
				ImGui::EndPopup();
				return;
			}
			if (ImGui::MenuItem("Open in Explorer")) {
				RevealAssetInExplorer(entry.Path);
			}
			ImGui::Separator();

			if (ImGui::MenuItem("Delete")) {
				const std::vector<std::string> paths = GetSelectedPaths();
				for (const std::string& path : paths) {
					DeleteEntry(path);
				}
				ImGui::EndPopup();
				return;
			}
			if (ImGui::MenuItem("Rename", nullptr, false, GetSelectedPaths().size() == 1)) {
				BeginRename(entry.Path, entry.Name);
			}
			if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
				DuplicateSelectedAssets();
			}
			if (ImGui::MenuItem("Copy Path")) {
				CopyPathToClipboard(entry.Path);
			}

			ImGui::EndPopup();
		}
	}

	void AssetBrowser::HandleDragSource(const DirectoryEntry& entry) {
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
			const char* pathStr = entry.Path.c_str();
			ImGui::SetDragDropPayload("ASSET_BROWSER_ITEM", pathStr, entry.Path.size() + 1);
			ImGui::Text("Move: %s", entry.Name.c_str());
			ImGui::EndDragDropSource();
		}
	}

	void AssetBrowser::HandleDropTarget(const DirectoryEntry& entry) {
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
				std::string sourcePath(static_cast<const char*>(payload->Data));

				if (sourcePath != entry.Path) {
					if (Directory::Move(sourcePath, entry.Path)) {
						m_Thumbnails.Invalidate(sourcePath);
						if (m_SelectedPath == sourcePath) {
							m_SelectedPath.clear();
						}
						m_SelectedPaths.erase(
							std::remove(m_SelectedPaths.begin(), m_SelectedPaths.end(), sourcePath),
							m_SelectedPaths.end());
						if (m_PressedPath == sourcePath) {
							m_PressedPath.clear();
						}
						m_NeedsRefresh = true;
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
	}


}
