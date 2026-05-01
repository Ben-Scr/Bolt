#pragma once
#include "Packages/PackageManager.hpp"
#include <future>
#include <string>
#include <vector>

namespace Axiom {

	class PackageManagerPanel {
	public:
		void Initialize(PackageManager* manager);
		void Shutdown();
		void Render();

	private:
		void RenderBrowseTab();
		void RenderInstalledTab();
		void RenderPackageRow(const PackageInfo& pkg, int index);
		void RenderStatusBar();

		void TriggerSearch();

		PackageManager* m_Manager = nullptr;

		// Source & search state
		int m_SelectedSource = 0;
		char m_SearchBuffer[256]{};
		std::string m_LastQuery;

		// Async search
		bool m_IsSearching = false;
		std::future<std::vector<PackageInfo>> m_SearchFuture;
		std::vector<PackageInfo> m_SearchResults;

		// Async operation (install/remove)
		bool m_IsOperating = false;
		std::future<PackageOperationResult> m_OperationFuture;
		std::string m_OperationTarget;
		std::string m_OperationVersion;
		bool m_OperationWasInstall = false;

		// Status
		std::string m_StatusMessage;
		bool m_StatusIsError = false;

		// Installed cache
		std::vector<PackageInfo> m_InstalledPackages;
		bool m_InstalledDirty = true;
	};

}
