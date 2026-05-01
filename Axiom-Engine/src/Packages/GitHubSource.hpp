#pragma once
#include "Packages/PackageSource.hpp"
#include <string>

namespace Axiom {

	class GitHubSource : public PackageSource {
	public:
		GitHubSource(const std::string& toolExePath, const std::string& indexUrl,
			const std::string& displayName = "Engine Packages");

		std::string GetName() const override { return m_DisplayName; }
		PackageSourceType GetType() const override { return PackageSourceType::GitHub; }

		std::vector<PackageInfo> Search(const std::string& query, int take) override;

		PackageOperationResult Install(const std::string& packageId,
			const std::string& version, const std::string& csprojPath) override;

		PackageOperationResult Remove(const std::string& packageId,
			const std::string& csprojPath) override;

		void InvalidateCache() { m_CachedIndex.clear(); }

	private:
		void EnsureIndex();
		std::string RunTool(const std::vector<std::string>& args) const;

		std::string m_ToolExePath;
		std::string m_IndexUrl;
		std::string m_DisplayName;
		std::vector<PackageInfo> m_CachedIndex;
		bool m_IndexLoaded = false;
	};

}
