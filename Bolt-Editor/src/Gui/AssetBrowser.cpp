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
#include <filesystem>
#include <fstream>

#include <thread>

#ifdef BT_PLATFORM_WINDOWS
#include <windows.h>
#include <shellapi.h>
#endif

namespace Bolt {
	namespace {
		bool IsLeftMouseDragPastClickThreshold()
		{
			const ImGuiIO& io = ImGui::GetIO();
			const ImVec2 delta(io.MousePos.x - io.MouseClickedPos[ImGuiMouseButton_Left].x,
				io.MousePos.y - io.MouseClickedPos[ImGuiMouseButton_Left].y);
			return (delta.x * delta.x + delta.y * delta.y) > (io.MouseDragThreshold * io.MouseDragThreshold);
		}
	}

	static const char* GetFileTypeIconName(const std::string& extension) {
		std::string ext = extension;
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

		if (ext == ".cs")                                                    return "asset_cs";
		if (ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp")    return "asset_cpp";
		if (ext == ".scene" || ext == ".bolt")                               return "asset_scene";
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
		m_SelectedPath.clear();
		m_PressedPath.clear();
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

				BoltProject* project = ProjectManager::GetCurrentProject();
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
			m_SelectedPath.clear();
			m_PressedPath.clear();
			CancelRename();
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

		const bool isSelected = (m_SelectedPath == entry.Path);

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
			Texture2D* tex = nullptr;
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
				m_SelectedPath = entry.Path;
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

		RenderItemContextMenu(entry);

		HandleDragSource(entry);
		if (entry.IsDirectory) {
			HandleDropTarget(entry);
		}

		ImGui::PopID();

		ImGui::SameLine(0, 0);
		ImGui::Dummy(ImVec2(m_TilePadding, 0));
	}


	void AssetBrowser::RenderGridContextMenu() {
		if (!m_ItemRightClicked &&
			ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
			ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
		{
			ImGui::OpenPopup("##AssetGridCtx");
		}

		if (ImGui::BeginPopup("##AssetGridCtx")) {
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
				if (ImGui::MenuItem("Folder")) {
					CreateFolder(m_CurrentDirectory);
				}
				ImGui::EndMenu();
			}

			ImGui::EndPopup();
		}
	}

	void AssetBrowser::RenderItemContextMenu(const DirectoryEntry& entry) {
		if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
			ImGui::OpenPopup("##ItemCtx");
			m_ItemRightClicked = true;
		}

		if (ImGui::BeginPopup("##ItemCtx")) {
			m_SelectedPath = entry.Path;

			if (ImGui::MenuItem("Delete")) {
				DeleteEntry(entry.Path);
				ImGui::EndPopup();
				return;
			}
			if (ImGui::MenuItem("Rename")) {
				BeginRename(entry.Path, entry.Name);
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
