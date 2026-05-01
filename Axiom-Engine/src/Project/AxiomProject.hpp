#pragma once
#include "Core/Export.hpp"
#include <string>
#include <functional>
#include <string_view>
#include <vector>

namespace Axiom {

	struct AXIOM_API AxiomProject {
		using CreateProgressCallback = std::function<void(float progress, std::string_view stage)>;

		struct GlobalSystemRegistration {
			std::string ClassName;
			bool Active = true;
		};

		std::string Name;
		std::string RootDirectory;
		std::string AssetsDirectory;
		std::string ScriptsDirectory;
		std::string ScenesDirectory;
		std::string AxiomAssetsDirectory;
		std::string NativeScriptsDir;
		std::string NativeSourceDir;
		std::string PackagesDirectory;
		std::string CsprojPath;
		std::string SlnPath;
		std::string ProjectFilePath;
		std::string EngineVersion;

		// Persistence
		std::string StartupScene = "SampleScene";
		std::string LastOpenedScene = "SampleScene";
		std::string GameViewAspect = "Free Aspect";
		bool GameViewVsync = true;

		// Build settings
		int BuildWidth = 1280;
		int BuildHeight = 720;
		bool BuildFullscreen = false;
		bool BuildResizable = true;
		std::string AppIconPath;
		std::vector<std::string> BuildSceneList;
		std::vector<GlobalSystemRegistration> GlobalSystems;

		// Names of packages (engine-shipped or project-local) that this project includes.
		// The premake loader filters manifests by this list when --axiom-project=<path>
		// is set. Empty vector + missing JSON field == legacy "scan everything" mode.
		std::vector<std::string> Packages;

		std::string GetUserAssemblyOutputPath(std::string_view configuration = {}) const;
		std::string GetNativeDllPath() const;
		std::string GetSceneFilePath(const std::string& sceneName) const;
		void EnsureNativeScriptBootstrapFiles() const;
		void EnsureNativeScriptProjectFiles() const;
		bool HasNativeScriptSources() const;

		void Save() const;

		static std::string GetActiveBuildConfiguration();
		static std::string GetActiveBuildDefineConstant();
		static std::string BuildManagedDefineConstants(std::string_view primarySymbol);

		static AxiomProject Create(const std::string& name, const std::string& parentDir,
			const CreateProgressCallback& progressCallback = {});
		static AxiomProject Load(const std::string& rootDir);
		static bool Validate(const std::string& rootDir);
		static bool IsValidProjectName(const std::string& name);
		static std::string GetDefaultProjectsDir();

		// Locate the engine repository root from the running executable (walks up the bin/ tree
		// until it finds Axiom-Engine/src + Axiom-ScriptCore.csproj). Empty string on failure.
		static std::string GetEngineRootDir();

		// Path to the bundled premake5 binary (vendor/bin/premake5.exe). Empty if not found.
		static std::string GetPremakePath();

		// Re-runs the engine's premake5 with --axiom-project=<projectRootDir> to regenerate
		// the engine solution so it includes that project's package list. Returns the
		// process result (non-zero exit code on failure). Safe no-op if premake isn't found
		// (returns a synthetic result with ExitCode != 0 and an error message in Output).
		struct RegenerateResult {
			bool   Succeeded = false;
			int    ExitCode = -1;
			std::string Output;
		};
		static RegenerateResult RegenerateSolutionForProject(const std::string& projectRootDir);

		// Locate MSBuild.exe for VS 2022 (Community/Professional/Enterprise/BuildTools),
		// falling back to vswhere.exe and finally to PATH lookup. Empty string on failure.
		static std::string GetMSBuildPath();

		// Run MSBuild on the engine's Axiom.sln. Returns process result. Caller should
		// call RegenerateSolutionForProject first if the project's package list changed.
		struct BuildResult {
			bool   Succeeded = false;
			int    ExitCode = -1;
			std::string Output;
		};
		static BuildResult BuildSolution(const std::string& configuration = "Debug",
			const std::string& platform = "x64");

		// Convenience: regenerate + build for a project in one call. Stops early on
		// regen failure; build runs only if regen succeeds (or if premake was missing,
		// which is a benign warning).
		struct AutomateResult {
			RegenerateResult Regenerate;
			BuildResult      Build;
			bool             RanBuild = false; // false if regen errored or no MSBuild
		};
		static AutomateResult AutomateForProject(const std::string& projectRootDir,
			const std::string& configuration = "Debug",
			const std::string& platform = "x64");
	};

} // namespace Axiom
