#pragma once
#include "Packages/PackageSource.hpp"
#include "Core/Export.hpp"

#include <atomic>
#include <memory>
#include <future>
#include <string>
#include <vector>

namespace Axiom {

	class AXIOM_API PackageManager {
	public:
		void Initialize(const std::string& toolExePath = {});
		void Shutdown();

		bool IsReady() const { return m_SharedState->IsReady.load(std::memory_order_acquire); }
		const std::string& GetToolPath() const { return m_ToolExePath; }

		void AddSource(std::unique_ptr<PackageSource> source);
		const std::vector<std::shared_ptr<PackageSource>>& GetSources() const { return m_Sources; }
		PackageSource* GetSource(int index);

		// Async operations — return futures polled by the UI each frame
		std::future<std::vector<PackageInfo>> SearchAsync(int sourceIndex,
			const std::string& query, int take = 20);

		std::future<PackageOperationResult> InstallAsync(int sourceIndex,
			const std::string& packageId, const std::string& version);

		std::future<PackageOperationResult> RemoveAsync(int sourceIndex,
			const std::string& packageId);

		// After install/remove: restore + rebuild + signal reload
		PackageOperationResult RestoreAndRebuild();

		// Read installed packages from the .csproj
		std::vector<PackageInfo> GetInstalledPackages() const;

		bool NeedsReload() const { return m_SharedState->NeedsReload.load(std::memory_order_acquire); }
		void ClearReloadFlag() { m_SharedState->NeedsReload.store(false, std::memory_order_release); }

	private:
		struct SharedState {
			std::atomic<bool> IsReady = false;
			std::atomic<bool> NeedsReload = false;
		};

		std::string GetCsprojPath() const;
		std::shared_ptr<PackageSource> GetSourceHandle(int index) const;

		std::vector<std::shared_ptr<PackageSource>> m_Sources;
		std::shared_ptr<SharedState> m_SharedState = std::make_shared<SharedState>();
		std::string m_ToolExePath;
	};

}
