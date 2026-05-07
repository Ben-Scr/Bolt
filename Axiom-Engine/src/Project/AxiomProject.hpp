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

		// Profiler — per-project preferences. Read at Load(), written at Save().
		// All fields are optional in axiom-project.json; missing values keep the
		// defaults below. The "ModuleEnabled" map is only populated for modules
		// the user has explicitly toggled (so newly-added engine modules default
		// to enabled without forcing a settings migration).
		struct ProfilerSettings {
			bool EnableInRuntime = false; // Ctrl+F6 panel in shipped runtime; off by default
			bool TrackInBackground = false; // collect even with the panel closed
			int  SamplingHz = 60;
			int  TrackingSpan = 200;
			std::vector<std::pair<std::string, bool>> ModuleEnabled;
		} Profiler;

		// Runtime stats overlay (F6 in built games). When true, the runtime
		// pushes RuntimeStatsLayer at startup so the user can toggle the
		// FPS / tris / memory / audio overlay with F6. When false the layer
		// isn't pushed at all — zero overhead in the shipped exe. Default
		// true so the diagnostic is available out of the box; ship-ready
		// builds can flip it off via Project Settings.
		bool ShowRuntimeStats = true;

		// Runtime log overlay (F7 in built games). Same model as
		// ShowRuntimeStats: the runtime pushes RuntimeLogLayer at startup
		// when this is true. When both overlays are visible, the log
		// window stacks below the stats window so they don't overlap.
		bool ShowRuntimeLogs = true;

		// Build profile — drives compile-time defines emitted to user
		// scripts at build time:
		//   • Development → AXIOM_BUILD_DEVELOPMENT (and toggle defaults
		//     for ShowRuntimeStats/ShowRuntimeLogs are ON)
		//   • Release     → AXIOM_BUILD_RELEASE (defaults OFF)
		// Independent of MSBuild's Debug/Release/Dist (which controls
		// ENGINE compilation). Set in the Build panel; default is
		// Development so iteration-day defaults are sensible.
		enum class BuildProfile : uint8_t {
			Development = 0,
			Release     = 1,
		};
		BuildProfile ActiveBuildProfile = BuildProfile::Development;

		// User-defined preprocessor symbols. Get baked into BOTH the C#
		// .csproj's <DefineConstants> AND the native scripts' CMakeLists
		// target_compile_definitions on every script compile. Useful for
		// per-team / per-build flags (STEAM_BUILD, DEMO_MODE, etc.).
		// Names only — no `=value` form (matches Unity's scripting-define-
		// symbols convention; keeps the UI a flat list and the symbols
		// uniform across both compilers).
		std::vector<std::string> CustomDefines;

		std::string GetUserAssemblyOutputPath(std::string_view configuration = {}) const;

		// Returns "AXIOM_BUILD_DEVELOPMENT" or "AXIOM_BUILD_RELEASE" — the
		// compile-time symbol matching ActiveBuildProfile. Used by the C#
		// build pipeline (BuildManagedDefineConstants) and the native
		// CMakeLists generator.
		std::string GetActiveBuildProfileDefine() const;
		static const char* BuildProfileToString(BuildProfile profile);
		static BuildProfile BuildProfileFromString(std::string_view value);

		std::string GetNativeDllPath() const;
		std::string GetSceneFilePath(const std::string& sceneName) const;
		void EnsureNativeScriptBootstrapFiles() const;
		void EnsureNativeScriptProjectFiles() const;
		bool HasNativeScriptSources() const;

		// Returns true on a confirmed atomic write to disk. Callers that depend on the
		// project file having reached the disk (e.g. before clearing dirty state, before
		// triggering project regen) MUST check the return value.
		bool Save() const;

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
		//
		// IMPORTANT: by default this builds the WHOLE solution — including Axiom-Engine,
		// Axiom-Editor, Axiom-Launcher, Axiom-Runtime. If the *running process* has any
		// of those DLLs loaded (it always does — the launcher has Axiom-Engine.dll
		// loaded for its own runtime), MSBuild's link step will fail with LNK1104 on
		// the locked .ilk/.dll. For the common project-create / project-open path,
		// prefer the overload taking explicit targets and pass only the project-local
		// package projects (`Pkg.<Name>.Native`, `Pkg.<Name>`).
		struct BuildResult {
			bool   Succeeded = false;
			int    ExitCode = -1;
			std::string Output;
		};
		static BuildResult BuildSolution(const std::string& configuration = "Debug",
			const std::string& platform = "x64");

		// Selective build: passes -t:<target1>;<target2>;... to MSBuild so only the
		// listed projects get built. Empty target list is a no-op (returns Success
		// with no MSBuild invocation). Use this for project-local-package builds
		// where the engine itself is already up to date and shouldn't be relinked.
		static BuildResult BuildSolutionTargets(const std::vector<std::string>& targets,
			const std::string& configuration = "Debug",
			const std::string& platform = "x64");

		// Enumerate names of axiom-package.lua manifests under <projectRoot>/Packages/.
		// Returns the directory names (which is the package name by convention).
		// Empty vector when the project has no Packages/ subdir or no manifests in it.
		static std::vector<std::string> EnumerateProjectLocalPackages(const std::string& projectRoot);

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
