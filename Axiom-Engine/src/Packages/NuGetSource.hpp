#pragma once
#include "Packages/PackageSource.hpp"
#include <string>

namespace Axiom {

	class NuGetSource : public PackageSource {
	public:
		explicit NuGetSource(const std::string& toolExePath);

		std::string GetName() const override { return "NuGet"; }
		PackageSourceType GetType() const override { return PackageSourceType::NuGet; }

		std::vector<PackageInfo> Search(const std::string& query, int take) override;

		PackageOperationResult Install(const std::string& packageId,
			const std::string& version, const std::string& csprojPath) override;

		PackageOperationResult Remove(const std::string& packageId,
			const std::string& csprojPath) override;

	private:
		std::string RunTool(const std::vector<std::string>& args) const;
		std::string m_ToolExePath;
	};

}
