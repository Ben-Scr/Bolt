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
	};

} // namespace Axiom
