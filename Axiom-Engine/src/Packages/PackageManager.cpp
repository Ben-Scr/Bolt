#include "pch.hpp"
#include "Packages/PackageManager.hpp"
#include "Project/ProjectManager.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/File.hpp"
#include "Utils/Process.hpp"

#include <cereal/macros.hpp>
#include <cereal/external/rapidxml/rapidxml.hpp>

#include <fstream>
#include <filesystem>

namespace Axiom {
	namespace {
		namespace rapidxml = cereal::rapidxml;

		struct XmlFileDocument {
			std::string Buffer;
			rapidxml::xml_document<> Document;
		};

		std::string Trim(std::string_view value) {
			const size_t start = value.find_first_not_of(" \t\r\n");
			if (start == std::string_view::npos) {
				return {};
			}

			const size_t end = value.find_last_not_of(" \t\r\n");
			return std::string(value.substr(start, end - start + 1));
		}

		bool LoadXmlDocument(const std::filesystem::path& path, XmlFileDocument& document) {
			std::ifstream input(path);
			if (!input.is_open()) {
				return false;
			}

			document.Buffer.assign((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
			document.Buffer.push_back('\0');
			document.Document.clear();

			try {
				document.Document.parse<rapidxml::parse_non_destructive>(&document.Buffer[0]);
				return true;
			}
			catch (const rapidxml::parse_error& e) {
				AIM_CORE_WARN_TAG("PackageManager", "Failed to parse XML '{}': {}", path.string(), e.what());
				return false;
			}
		}

		std::string GetAttributeValue(const rapidxml::xml_node<>* node, const char* attributeName) {
			if (!node) {
				return {};
			}

			if (const auto* attribute = node->first_attribute(attributeName)) {
				return Trim(attribute->value());
			}

			return {};
		}

		std::string GetFirstChildValue(const rapidxml::xml_node<>* node, const char* childName) {
			if (!node) {
				return {};
			}

			if (const auto* child = node->first_node(childName)) {
				return Trim(child->value());
			}

			return {};
		}

		std::string GetItemIdentity(const rapidxml::xml_node<>* node) {
			std::string identity = GetAttributeValue(node, "Include");
			if (!identity.empty()) {
				return identity;
			}

			return GetAttributeValue(node, "Update");
		}

		std::string GetPackageVersion(const rapidxml::xml_node<>* node) {
			std::string version = GetAttributeValue(node, "Version");
			if (!version.empty()) {
				return version;
			}

			version = GetAttributeValue(node, "VersionOverride");
			if (!version.empty()) {
				return version;
			}

			version = GetFirstChildValue(node, "Version");
			if (!version.empty()) {
				return version;
			}

			return GetFirstChildValue(node, "VersionOverride");
		}

		std::string GetSimpleAssemblyName(std::string identity) {
			const size_t commaPos = identity.find(',');
			if (commaPos != std::string::npos) {
				identity = identity.substr(0, commaPos);
			}

			return Trim(identity);
		}

		std::unordered_map<std::string, std::string> LoadCentralPackageVersions(const std::filesystem::path& csprojPath) {
			std::unordered_map<std::string, std::string> versions;
			std::vector<std::filesystem::path> propsFiles;

			std::filesystem::path currentDirectory = csprojPath.parent_path();
			while (!currentDirectory.empty()) {
				const std::filesystem::path propsPath = currentDirectory / "Directory.Packages.props";
				if (std::filesystem::exists(propsPath)) {
					propsFiles.push_back(propsPath);
				}

				const std::filesystem::path parent = currentDirectory.parent_path();
				if (parent == currentDirectory) {
					break;
				}

				currentDirectory = parent;
			}

			for (auto it = propsFiles.rbegin(); it != propsFiles.rend(); ++it) {
				XmlFileDocument propsDocument;
				if (!LoadXmlDocument(*it, propsDocument)) {
					continue;
				}

				const rapidxml::xml_node<>* root = propsDocument.Document.first_node("Project");
				if (!root) {
					continue;
				}

				for (const auto* itemGroup = root->first_node("ItemGroup"); itemGroup; itemGroup = itemGroup->next_sibling("ItemGroup")) {
					for (const auto* node = itemGroup->first_node("PackageVersion"); node; node = node->next_sibling("PackageVersion")) {
						const std::string packageId = GetItemIdentity(node);
						if (packageId.empty()) {
							continue;
						}

						const std::string version = GetPackageVersion(node);
						if (!version.empty()) {
							versions[packageId] = version;
						}
					}
				}
			}

			return versions;
		}

		void AddOrUpdateInstalledPackage(std::vector<PackageInfo>& installed, PackageInfo info) {
			for (auto& existing : installed) {
				if (existing.Id != info.Id || existing.SourceType != info.SourceType) {
					continue;
				}

				if (existing.Version.empty() && !info.Version.empty()) {
					existing.Version = info.Version;
					existing.InstalledVersion = info.Version;
				}
				return;
			}

			installed.push_back(std::move(info));
		}

		std::string GetPackageToolExecutableName() {
#if defined(AIM_PLATFORM_WINDOWS)
			return "Axiom-PackageTool.exe";
#else
			return "Axiom-PackageTool";
#endif
		}

		std::vector<std::string> GetPreferredPackageToolConfigurations() {
			return {
				AxiomProject::GetActiveBuildConfiguration(),
				"Release",
				"Debug",
				"Dist"
			};
		}

		std::vector<std::filesystem::path> GetPackageToolCandidates(
			const std::filesystem::path& executableDirectory,
			const std::filesystem::path& explicitPath) {
			std::vector<std::filesystem::path> candidates;
			if (!explicitPath.empty()) {
				candidates.push_back(explicitPath);
			}

			const std::filesystem::path projectDirectory = executableDirectory / ".." / ".." / ".." / "Axiom-PackageTool";
			for (const std::string& configuration : GetPreferredPackageToolConfigurations()) {
				const std::filesystem::path outputDirectory = projectDirectory / "bin" / configuration / "net9.0";
				candidates.push_back(outputDirectory / GetPackageToolExecutableName());
				candidates.push_back(outputDirectory / "Axiom-PackageTool.dll");
			}

			candidates.push_back(executableDirectory / GetPackageToolExecutableName());
			candidates.push_back(executableDirectory / "Axiom-PackageTool.dll");
			return candidates;
		}

		PackageOperationResult RestoreAndRebuildProject(const std::string& csprojPath) {
			if (csprojPath.empty()) {
				return { false, "No project loaded" };
			}

			Process::Result restoreResult = Process::Run({
				"dotnet",
				"restore",
				csprojPath,
				"--nologo",
				"-v", "q"
			});
			if (!restoreResult.Succeeded()) {
				AIM_CORE_ERROR_TAG("PackageManager", "dotnet restore failed (exit code {})", restoreResult.ExitCode);
				if (!restoreResult.Output.empty()) {
					AIM_CORE_ERROR_TAG("PackageManager", "{}", restoreResult.Output);
				}
				return { false, "dotnet restore failed" };
			}

			Process::Result buildResult = Process::Run({
				"dotnet",
				"build",
				csprojPath,
				"-c", AxiomProject::GetActiveBuildConfiguration(),
				"--nologo",
				"-v", "q",
				"-p:DefineConstants=" + AxiomProject::BuildManagedDefineConstants("AXIOM_EDITOR")
			});
			if (!buildResult.Succeeded()) {
				AIM_CORE_ERROR_TAG("PackageManager", "dotnet build failed (exit code {})", buildResult.ExitCode);
				if (!buildResult.Output.empty()) {
					AIM_CORE_ERROR_TAG("PackageManager", "{}", buildResult.Output);
				}
				return { false, "dotnet build failed" };
			}

			AIM_CORE_INFO_TAG("PackageManager", "Restore and rebuild complete");
			return { true, "Build succeeded" };
		}
	}

	void PackageManager::Initialize(const std::string& toolExePath) {
		m_SharedState->IsReady.store(false, std::memory_order_release);
		m_SharedState->NeedsReload.store(false, std::memory_order_release);
		m_ToolExePath.clear();

		// Build candidate paths — always try canonical resolution
		auto exeDir = std::filesystem::path(Path::ExecutableDir());
		std::vector<std::filesystem::path> candidates = GetPackageToolCandidates(exeDir, std::filesystem::path(toolExePath));

		for (const auto& candidate : candidates) {
			if (!candidate.empty() && std::filesystem::exists(candidate)) {
				m_ToolExePath = std::filesystem::canonical(candidate).string();
				AIM_CORE_INFO_TAG("PackageManager", "Found package tool at {}", m_ToolExePath);
				break;
			}
		}

		if (!m_ToolExePath.empty() && std::filesystem::exists(m_ToolExePath)) {
			m_SharedState->IsReady.store(true, std::memory_order_release);
			AIM_CORE_INFO_TAG("PackageManager", "Package manager initialized");
		}
		else {
			// Try building the tool
			auto exeDir = std::filesystem::path(Path::ExecutableDir());
			auto projectDir = exeDir / ".." / ".." / ".." / "Axiom-PackageTool";
			if (std::filesystem::exists(projectDir / "Axiom-PackageTool.csproj")) {
				AIM_CORE_INFO_TAG("PackageManager", "Building package tool...");
				const std::string buildConfiguration = AxiomProject::GetActiveBuildConfiguration();
				const Process::Result buildResult = Process::Run({
					"dotnet",
					"build",
					projectDir.string(),
					"-c", buildConfiguration,
					"--nologo",
					"-v", "q"
				});
				if (buildResult.Succeeded()) {
					for (const auto& candidate : GetPackageToolCandidates(exeDir, {})) {
						if (!candidate.empty() && std::filesystem::exists(candidate)) {
							m_ToolExePath = std::filesystem::canonical(candidate).string();
							m_SharedState->IsReady.store(true, std::memory_order_release);
							AIM_CORE_INFO_TAG("PackageManager", "Package tool built and ready");
							break;
						}
					}
					if (!m_SharedState->IsReady.load(std::memory_order_acquire)) {
						AIM_CORE_WARN_TAG("PackageManager", "Package tool build succeeded, but no runnable artifact was found");
					}
				}
				else {
					AIM_CORE_ERROR_TAG("PackageManager", "Failed to build package tool (exit code {})", buildResult.ExitCode);
					if (!buildResult.Output.empty()) {
						AIM_CORE_ERROR_TAG("PackageManager", "{}", buildResult.Output);
					}
				}
			}
			else {
				AIM_CORE_WARN_TAG("PackageManager", "Package tool project not found, package manager disabled");
			}
		}
	}

	void PackageManager::Shutdown() {
		m_SharedState->NeedsReload.store(false, std::memory_order_release);
		m_SharedState->IsReady.store(false, std::memory_order_release);
		m_Sources.clear();
	}

	void PackageManager::AddSource(std::unique_ptr<PackageSource> source) {
		if (!source) {
			return;
		}

		m_Sources.emplace_back(std::move(source));
	}

	PackageSource* PackageManager::GetSource(int index) {
		return GetSourceHandle(index).get();
	}

	std::shared_ptr<PackageSource> PackageManager::GetSourceHandle(int index) const {
		if (index < 0 || index >= static_cast<int>(m_Sources.size()))
			return {};
		return m_Sources[static_cast<size_t>(index)];
	}

	std::string PackageManager::GetCsprojPath() const {
		AxiomProject* project = ProjectManager::GetCurrentProject();
		return project ? project->CsprojPath : "";
	}

	std::future<std::vector<PackageInfo>> PackageManager::SearchAsync(int sourceIndex,
		const std::string& query, int take) {

		std::shared_ptr<PackageSource> source = GetSourceHandle(sourceIndex);
		if (!source) {
			return std::async(std::launch::deferred, []() -> std::vector<PackageInfo> { return {}; });
		}

		// Mark installed packages
		auto installed = GetInstalledPackages();

		return std::async(std::launch::async, [source = std::move(source), query, take, installed = std::move(installed)]() mutable -> std::vector<PackageInfo> {
			auto results = source->Search(query, take);

			// Cross-reference with installed packages. NuGet IDs are case-insensitive
			// per the package-spec; `dotnet add` may write a different case into the
			// .csproj than what the search API returns, so compare case-folded.
			auto equalsIgnoreCase = [](const std::string& a, const std::string& b) {
				if (a.size() != b.size()) return false;
				for (size_t i = 0; i < a.size(); ++i) {
					const unsigned char ca = static_cast<unsigned char>(a[i]);
					const unsigned char cb = static_cast<unsigned char>(b[i]);
					if (std::tolower(ca) != std::tolower(cb)) return false;
				}
				return true;
			};
			for (auto& pkg : results) {
				for (const auto& inst : installed) {
					if (equalsIgnoreCase(pkg.Id, inst.Id)) {
						pkg.IsInstalled = true;
						pkg.InstalledVersion = inst.Version;
						break;
					}
				}
			}
			return results;
		});
	}

	std::future<PackageOperationResult> PackageManager::InstallAsync(int sourceIndex,
		const std::string& packageId, const std::string& version) {

		std::shared_ptr<PackageSource> source = GetSourceHandle(sourceIndex);
		std::string csproj = GetCsprojPath();
		std::shared_ptr<SharedState> sharedState = m_SharedState;

		if (!source || csproj.empty()) {
			return std::async(std::launch::deferred, []() -> PackageOperationResult {
				return { false, "No source or project loaded" };
			});
		}

		return std::async(std::launch::async, [source = std::move(source), packageId, version, csproj, sharedState = std::move(sharedState)]() -> PackageOperationResult {
			auto result = source->Install(packageId, version, csproj);
			if (result.Success) {
				auto rebuild = RestoreAndRebuildProject(csproj);
				if (!rebuild.Success)
					return { false, "Install succeeded but rebuild failed: " + rebuild.Message };
				sharedState->NeedsReload.store(true, std::memory_order_release);
			}
			return result;
		});
	}

	std::future<PackageOperationResult> PackageManager::RemoveAsync(int sourceIndex,
		const std::string& packageId) {

		std::shared_ptr<PackageSource> source = GetSourceHandle(sourceIndex);
		std::string csproj = GetCsprojPath();
		std::shared_ptr<SharedState> sharedState = m_SharedState;

		if (!source || csproj.empty()) {
			return std::async(std::launch::deferred, []() -> PackageOperationResult {
				return { false, "No source or project loaded" };
			});
		}

		return std::async(std::launch::async, [source = std::move(source), packageId, csproj, sharedState = std::move(sharedState)]() -> PackageOperationResult {
			auto result = source->Remove(packageId, csproj);
			if (result.Success) {
				auto rebuild = RestoreAndRebuildProject(csproj);
				if (!rebuild.Success)
					return { false, "Remove succeeded but rebuild failed: " + rebuild.Message };
				sharedState->NeedsReload.store(true, std::memory_order_release);
			}
			return result;
		});
	}

	PackageOperationResult PackageManager::RestoreAndRebuild() {
		return RestoreAndRebuildProject(GetCsprojPath());
	}

	std::vector<PackageInfo> PackageManager::GetInstalledPackages() const {
		std::vector<PackageInfo> installed;

		std::string csproj = GetCsprojPath();
		if (csproj.empty() || !File::Exists(csproj))
			return installed;

		XmlFileDocument projectDocument;
		if (!LoadXmlDocument(std::filesystem::path(csproj), projectDocument)) {
			return installed;
		}

		const auto centralPackageVersions = LoadCentralPackageVersions(std::filesystem::path(csproj));
		const rapidxml::xml_node<>* root = projectDocument.Document.first_node("Project");
		if (!root) {
			return installed;
		}

		for (const auto* itemGroup = root->first_node("ItemGroup"); itemGroup; itemGroup = itemGroup->next_sibling("ItemGroup")) {
			for (const auto* node = itemGroup->first_node(); node; node = node->next_sibling()) {
				const std::string nodeName = node->name();
				if (nodeName == "PackageReference") {
					const std::string packageId = GetItemIdentity(node);
					if (packageId.empty()) {
						continue;
					}

					PackageInfo info;
					info.Id = packageId;
					info.Version = GetPackageVersion(node);
					if (info.Version.empty()) {
						if (const auto it = centralPackageVersions.find(packageId); it != centralPackageVersions.end()) {
							info.Version = it->second;
						}
					}
					info.IsInstalled = true;
					info.InstalledVersion = info.Version;
					info.SourceType = PackageSourceType::NuGet;
					info.SourceName = "NuGet";
					AddOrUpdateInstalledPackage(installed, std::move(info));
					continue;
				}

				if (nodeName != "Reference") {
					continue;
				}

				std::string name = GetSimpleAssemblyName(GetItemIdentity(node));
				if (name.empty()) {
					continue;
				}

				// Skip system references
				if (name.find("System") == 0 || name.find("Microsoft") == 0)
					continue;

				PackageInfo info;
				info.Id = name;
				info.IsInstalled = true;
				info.SourceType = PackageSourceType::GitHub;
				info.SourceName = "Engine";
				AddOrUpdateInstalledPackage(installed, std::move(info));
			}
		}

		return installed;
	}

}
