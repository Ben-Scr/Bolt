#pragma once
#include "Packages/PackageInfo.hpp"
#include "Packages/PackageOperationResult.hpp"

#include <string>
#include <vector>

namespace Axiom {
	class PackageSource {
	public:
		virtual ~PackageSource() = default;

		virtual std::string GetName() const = 0;
		virtual PackageSourceType GetType() const = 0;

		virtual std::vector<PackageInfo> Search(const std::string& query, int take) = 0;

		virtual PackageOperationResult Install(const std::string& packageId,
			const std::string& version, const std::string& csprojPath) = 0;

		virtual PackageOperationResult Remove(const std::string& packageId,
			const std::string& csprojPath) = 0;
	};

}
