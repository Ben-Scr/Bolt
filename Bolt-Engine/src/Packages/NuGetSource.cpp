#include "pch.hpp"
#include "Packages/NuGetSource.hpp"
#include "Serialization/Json.hpp"
#include "Utils/Process.hpp"

#include <filesystem>

namespace Bolt {

	NuGetSource::NuGetSource(const std::string& toolExePath)
		: m_ToolExePath(toolExePath) {
	}

	std::string NuGetSource::RunTool(const std::vector<std::string>& args) const {
		std::vector<std::string> command;
		command.reserve(args.size() + 2);
		if (std::filesystem::path(m_ToolExePath).extension() == ".dll") {
			command.push_back("dotnet");
		}
		command.push_back(m_ToolExePath);
		command.insert(command.end(), args.begin(), args.end());

		Process::Result result = Process::Run(command);
		if (!result.Succeeded()) {
			BT_CORE_ERROR_TAG("NuGetSource", "Failed to run tool (exit code {})", result.ExitCode);
		}
		else if (result.Output.empty()) {
			BT_CORE_WARN_TAG("NuGetSource", "Tool returned empty output");
		}

		return result.Output;
	}

	std::vector<PackageInfo> NuGetSource::Search(const std::string& query, int take) {
		std::vector<PackageInfo> results;

		std::string output = RunTool({ "nuget-search", query, "--take", std::to_string(take) });
		Json::Value root;
		std::string parseError;
		if (!Json::TryParse(output, root, &parseError) || !root.IsArray()) {
			if (!output.empty()) {
				BT_CORE_WARN_TAG("NuGetSource", "Failed to parse search results: {}", parseError);
			}
			return results;
		}

		results.reserve(root.GetArray().size());

		for (const Json::Value& item : root.GetArray()) {
			if (!item.IsObject()) {
				continue;
			}

			PackageInfo info;
			if (const Json::Value* idValue = item.FindMember("id")) info.Id = idValue->AsStringOr();
			if (const Json::Value* versionValue = item.FindMember("version")) info.Version = versionValue->AsStringOr();
			if (const Json::Value* descriptionValue = item.FindMember("description")) info.Description = descriptionValue->AsStringOr();
			if (const Json::Value* authorsValue = item.FindMember("authors")) info.Authors = authorsValue->AsStringOr();
			if (const Json::Value* downloadsValue = item.FindMember("downloads")) info.TotalDownloads = downloadsValue->AsInt64Or(0);
			if (const Json::Value* verifiedValue = item.FindMember("verified")) info.Verified = verifiedValue->AsBoolOr(false);
			info.SourceName = "NuGet";
			info.SourceType = PackageSourceType::NuGet;

			if (!info.Id.empty())
				results.push_back(std::move(info));
		}

		return results;
	}

	PackageOperationResult NuGetSource::Install(const std::string& packageId,
		const std::string& version, const std::string& csprojPath) {

		BT_CORE_INFO_TAG("NuGetSource", "Installing {} {}...", packageId, version);
		std::vector<std::string> command = {
			"dotnet",
			"add",
			csprojPath,
			"package",
			packageId
		};
		if (!version.empty()) {
			command.push_back("--version");
			command.push_back(version);
		}
		Process::Result result = Process::Run(command);

		if (!result.Succeeded()) {
			return { false, "dotnet add package failed (exit code " + std::to_string(result.ExitCode) + ")" };
		}

		return { true, packageId + " " + version + " installed" };
	}

	PackageOperationResult NuGetSource::Remove(const std::string& packageId,
		const std::string& csprojPath) {

		BT_CORE_INFO_TAG("NuGetSource", "Removing {}...", packageId);
		Process::Result result = Process::Run({
			"dotnet",
			"remove",
			csprojPath,
			"package",
			packageId
		});

		if (!result.Succeeded()) {
			return { false, "dotnet remove package failed (exit code " + std::to_string(result.ExitCode) + ")" };
		}

		return { true, packageId + " removed" };
	}

}
