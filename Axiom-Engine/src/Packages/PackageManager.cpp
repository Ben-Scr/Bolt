#include "pch.hpp"
#include "Packages/PackageManager.hpp"
#include "Packages/CsprojParser.hpp"
#include "Project/ProjectManager.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/File.hpp"
#include "Utils/Process.hpp"

#include <fstream>
#include <filesystem>

namespace Axiom {
	namespace {
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

		PackageOperationResult RestoreAndRebuildProject(const std::string& csprojPath,
			const std::string& buildConfig,
			const std::string& defineConstantsArg) {
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
				"-c", buildConfig,
				"--nologo",
				"-v", "q",
				defineConstantsArg
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
		// Re-init must drop the previous run's source list. Without this clear,
		// reopening a project (or any caller that calls Initialize twice) gets a
		// duplicated source set and double-loaded packages.
		m_Sources.clear();

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

		// Snapshot AxiomProject statics on the calling thread so the worker
		// doesn't race with project reload (M9).
		const std::string buildConfig = AxiomProject::GetActiveBuildConfiguration();
		const std::string defineConstantsArg =
			"-p:DefineConstants=" + AxiomProject::BuildManagedDefineConstants("AXIOM_EDITOR");

		return std::async(std::launch::async, [source = std::move(source), packageId, version, csproj, sharedState = std::move(sharedState), buildConfig, defineConstantsArg]() -> PackageOperationResult {
			auto result = source->Install(packageId, version, csproj);
			if (result.Success) {
				auto rebuild = RestoreAndRebuildProject(csproj, buildConfig, defineConstantsArg);
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

		// Snapshot — see InstallAsync.
		const std::string buildConfig = AxiomProject::GetActiveBuildConfiguration();
		const std::string defineConstantsArg =
			"-p:DefineConstants=" + AxiomProject::BuildManagedDefineConstants("AXIOM_EDITOR");

		return std::async(std::launch::async, [source = std::move(source), packageId, csproj, sharedState = std::move(sharedState), buildConfig, defineConstantsArg]() -> PackageOperationResult {
			auto result = source->Remove(packageId, csproj);
			if (result.Success) {
				auto rebuild = RestoreAndRebuildProject(csproj, buildConfig, defineConstantsArg);
				if (!rebuild.Success)
					return { false, "Remove succeeded but rebuild failed: " + rebuild.Message };
				sharedState->NeedsReload.store(true, std::memory_order_release);
			}
			return result;
		});
	}

	PackageOperationResult PackageManager::RestoreAndRebuild() {
		const std::string buildConfig = AxiomProject::GetActiveBuildConfiguration();
		const std::string defineConstantsArg =
			"-p:DefineConstants=" + AxiomProject::BuildManagedDefineConstants("AXIOM_EDITOR");
		return RestoreAndRebuildProject(GetCsprojPath(), buildConfig, defineConstantsArg);
	}

	std::vector<PackageInfo> PackageManager::GetInstalledPackages() const {
		const std::string csproj = GetCsprojPath();
		if (csproj.empty() || !File::Exists(csproj)) {
			return {};
		}

		std::ifstream input(csproj);
		if (!input.is_open()) {
			return {};
		}
		const std::string xml((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		input.close();

		// Hand the disk-loaded XML and central-version map to the pure parser.
		// Keeping the disk I/O here means the parser stays unit-testable from a
		// raw string without needing a real file or project on disk.
		const auto centralVersions = LoadCentralPackageVersionsFromDisk(std::filesystem::path(csproj));
		return ParseInstalledPackagesFromXml(xml, centralVersions);
	}

}
