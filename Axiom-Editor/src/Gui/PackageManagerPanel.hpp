#pragma once
#include "Editor/AxiomPackageInstaller.hpp"
#include "Packages/PackageManager.hpp"

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Axiom {

	// Editor's Package Manager UI — two top-level tabs:
	//
	//   "Search Packages"      — discover + install
	//     ├── Axiom Packages   collapsing header listing engine-shipped packages
	//     └── User Packages    collapsing header with NuGet / GitHub-URL / Local-path subsections
	//
	//   "Installed Packages"   — review + uninstall, with a top-level search filter
	//     ├── Axiom Packages   currently-discovered engine packages
	//     └── User Packages    project-local packages + NuGet PackageReferences
	class PackageManagerPanel {
	public:
		void Initialize(PackageManager* manager);
		void Shutdown();
		void Render();

	private:
		// ── Top-level tabs ──────────────────────────────────────────────────────────
		void RenderSearchPackagesTab();
		void RenderInstalledPackagesTab();

		// ── Search tab sections ────────────────────────────────────────────────────
		void RenderAxiomRegistrySection();
		// Floating add-package windows opened from the "+" button's drop-down menu.
		void RenderGitInstallWindow();
		void RenderNuGetInstallWindow();
		// Disk-install runs inline (folder picker → install → done), no window.
		void HandleDiskInstall();

		// ── Installed tab sections ─────────────────────────────────────────────────
		void RenderInstalledAxiomPackagesSection();
		void RenderInstalledUserPackagesSection();

		// ── Shared helpers ─────────────────────────────────────────────────────────
		void RefreshManifestsIfDirty();
		void RenderLayerBadges(const AxiomPackageManifest& manifest);
		// Renders one row for an Axiom package with layer badges + an Install or
		// Uninstall button depending on whether the package is in project.Packages.
		// `mode` selects whether non-installed packages render an Install button
		// (Search tab) or are skipped entirely (Installed tab).
		enum class RowMode { ShowAll, InstalledOnly };
		void RenderAxiomPackageRow(const AxiomPackageManifest& manifest, const char* idHint, RowMode mode);
		void RenderNugetPackageRow(const PackageInfo& pkg, int index);
		void TriggerNuGetSearch();
		bool BrowseForLocalFolder(std::string& outPath);

		// True if `name` is in the active project's Packages allow-list.
		bool IsPackageInstalled(const std::string& name) const;

		// After a successful Axiom-package install or removal, regenerate the engine
		// solution + rebuild on a worker thread so the UI keeps responding while
		// MSBuild churns. Caller doesn't block; progress is rendered at the top of
		// the panel and the result is collected into the status strip on completion.
		void StartPostInstallAutomation();

		// Per-frame poll for the async automate worker; pulls completed results.
		void PollAutomationTask();

		struct AutomationTaskState {
			std::mutex Mutex;
			std::string Stage = "Idle";
			float Progress = 0.0f;
			std::atomic<bool> Running{ false };
			bool Finished = false;
			bool Success = false;
			std::string Error;
		};
		std::shared_ptr<AutomationTaskState> m_AutomationTask;
		std::thread m_AutomationWorker;

		// Win32 IFileOpenDialog::Show is a modal call that blocks the calling thread
		// while the dialog is up — running it on the editor's main thread freezes
		// the entire UI. We spawn a worker that owns its own STA, runs the dialog,
		// and writes the picked path back. The main thread polls and finishes the
		// install once the worker reports Finished.
		struct DiskInstallTaskState {
			std::mutex Mutex;
			std::atomic<bool> Running{ false };
			bool Finished = false;
			std::string PickedPath; // empty if the user cancelled
		};
		std::shared_ptr<DiskInstallTaskState> m_DiskInstallTask;
		std::thread m_DiskInstallWorker;
		void PollDiskInstallTask();

		PackageManager* m_Manager = nullptr;

		// Tab + filter state
		int m_TabIndex = 0;
		char m_InstalledFilterBuffer[256]{};
		char m_AxiomSearchFilterBuffer[256]{};

		// NuGet sub-panel state (kept compatible with the previous flow)
		int m_SelectedSource = 0;
		char m_NuGetSearchBuffer[256]{};
		std::string m_LastNuGetQuery;
		bool m_IsSearching = false;
		std::future<std::vector<PackageInfo>> m_SearchFuture;
		std::vector<PackageInfo> m_SearchResults;

		bool m_IsOperating = false;
		std::future<PackageOperationResult> m_OperationFuture;
		std::string m_OperationTarget;
		std::string m_OperationVersion;
		bool m_OperationWasInstall = false;

		// Floating-window state for the "+" menu options.
		bool m_ShowGitInstallWindow = false;
		bool m_ShowNuGetInstallWindow = false;
		char m_GitHubUrlBuffer[512]{};

		// Status strip at the bottom of the panel
		std::string m_StatusMessage;
		bool m_StatusIsError = false;

		// Cached enumerations
		std::vector<AxiomPackageManifest> m_AllManifests;
		bool m_ManifestsDirty = true;

		// NuGet installed cache (still useful for the Installed tab)
		std::vector<PackageInfo> m_InstalledNuGetPackages;
		bool m_InstalledNuGetDirty = true;
	};

}
