#pragma once
#include "Gui/ThumbnailCache.hpp"
#include "Scene/EntityHandle.hpp"
#include "Serialization/Directory.hpp"
#include <string>
#include <vector>

namespace Bolt {

	class AssetBrowser {
	public:
		void Initialize(const std::string& rootDirectory);
		void Shutdown();
		void Render();

		/// Returns true when the user is currently naming a new script (rename in progress).
		bool IsCreatingScript() const { return m_PendingScriptType != PendingScriptType::None; }
		bool BeginRenameSelected();

		std::string TakePendingSceneLoad() {
			std::string p = std::move(m_PendingSceneLoad);
			m_PendingSceneLoad.clear();
			return p;
		}

		/// Call this when external files are dropped onto the window.
		void OnExternalFileDrop(const std::vector<std::string>& paths);

		/// Returns the currently selected asset path (empty if none).
		const std::string& GetSelectedPath() const { return m_SelectedPath; }
		void ClearSelection() {
			ClearAssetSelection();
		}
		bool TakeSelectionActivated() {
			const bool activated = m_SelectionActivated;
			m_SelectionActivated = false;
			return activated;
		}

	private:
		void NavigateTo(const std::string& directory);
		void NavigateUp();
		void Refresh();

		void RenderBreadcrumb();
		void RenderGrid();
		void RenderAssetTile(const DirectoryEntry& entry, int index);

		void RenderGridContextMenu();
		void RenderItemContextMenu(const DirectoryEntry& entry, int index);

		void HandleDragSource(const DirectoryEntry& entry);
		void HandleDropTarget(const DirectoryEntry& entry);
		void OpenAssetExternal(const DirectoryEntry& entry);
		void OpenAssetPath(const std::string& path);
		void RevealAssetInExplorer(const std::string& path);

		void ClearAssetSelection();
		bool IsPathSelected(const std::string& path) const;
		std::vector<std::string> GetSelectedPaths() const;
		void SetSingleSelection(const std::string& path, int index);
		void ToggleSelection(const std::string& path, int index);
		void SelectRange(int index);
		void HandleAssetShortcuts();
		void CopySelectedAssets(bool cut);
		void PasteAssets();
		void DuplicateSelectedAssets();

		void DeleteEntry(const std::string& path);
		void RenameEntry(const std::string& path, const std::string& newName);
		void CopyPathToClipboard(const std::string& path);
		void CreateFolder(const std::string& parentDir);
		void CreateScript(const std::string& parentDir);
		void CreateNativeScript(const std::string& parentDir);
		void CreateCSharpComponent(const std::string& parentDir);
		void CreateGameSystem(const std::string& parentDir);
		void CreateGlobalSystem(const std::string& parentDir);
		void CreateNativeComponent(const std::string& parentDir);
		void CreateScene(const std::string& parentDir);
		void CreateEntityPrefab(const std::string& parentDir, EntityHandle sourceEntity = entt::null);

		void BeginRename(const std::string& path, const std::string& currentName);
		void CommitRename();
		void CancelRename();
		bool IsRenamingEntry(const std::string& path) const;

		std::string m_RootDirectory;
		std::string m_CurrentDirectory;
		std::vector<DirectoryEntry> m_Entries;
		std::vector<std::string> m_VisibleEntryPaths;
		std::string m_SelectedPath;
		std::vector<std::string> m_SelectedPaths;
		int m_LastSelectionIndex = -1;
		std::string m_PressedPath;
		bool m_SelectionActivated = false;
		bool m_NeedsRefresh = true;

		bool m_IsRenaming = false;
		std::string m_RenamePath;
		char m_RenameBuffer[256]{};
		int m_RenameFrameCounter = 0;

		float m_TileSize = 80.0f;
		float m_TilePadding = 8.0f;

		bool m_ItemRightClicked = false;
		std::string m_PendingSceneLoad;
		std::vector<std::string> m_AssetClipboardPaths;
		bool m_AssetClipboardCut = false;

		// Deferred script creation - boilerplate/project script is written after rename is committed.
		enum class PendingScriptType { None, CSharp, Native, CSharpComponent, CSharpGameSystem, CSharpGlobalSystem, NativeComponent, EntityPrefab };
		PendingScriptType m_PendingScriptType = PendingScriptType::None;
		std::string m_PendingScriptDir;  // parent directory for the new script
		EntityHandle m_PendingPrefabSourceEntity = entt::null;

		ThumbnailCache m_Thumbnails;

		// Pending OS file drops — set externally, consumed in Render()
		std::vector<std::string> m_PendingExternalDrops;
	};

}
