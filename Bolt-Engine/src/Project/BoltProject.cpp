#include "pch.hpp"
#include "Project/BoltProject.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/Directory.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Core/Log.hpp"
#include "Core/Version.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <random>

namespace Bolt {
	namespace {
		void ReportCreateProgress(const BoltProject::CreateProgressCallback& callback, float progress, std::string_view stage) {
			if (callback) {
				callback(progress, stage);
			}
		}

		bool ToLocalTime(std::time_t value, std::tm& outTime) {
#if defined(_WIN32)
			return localtime_s(&outTime, &value) == 0;
#else
			return localtime_r(&value, &outTime) != nullptr;
#endif
		}

		std::string EscapeXml(const std::string& value) {
			std::string escaped;
			escaped.reserve(value.size());

			for (char c : value) {
				switch (c) {
				case '&':  escaped += "&amp;";  break;
				case '<':  escaped += "&lt;";   break;
				case '>':  escaped += "&gt;";   break;
				case '"':  escaped += "&quot;"; break;
				case '\'': escaped += "&apos;"; break;
				default:   escaped += c;        break;
				}
			}

			return escaped;
		}

		std::string MakeRelativePathOrEmpty(const std::filesystem::path& from, const std::filesystem::path& to) {
			std::error_code ec;
			const std::filesystem::path relative = std::filesystem::relative(to, from, ec);
			if (ec) {
				return {};
			}

			return relative.generic_string();
		}

		std::filesystem::path ResolveEngineRoot() {
			const auto exeDir = std::filesystem::path(Path::ExecutableDir());
			const std::vector<std::filesystem::path> candidates = {
				exeDir / ".." / ".." / "..",
				exeDir / ".."
			};

			for (const auto& candidate : candidates) {
				std::error_code ec;
				const std::filesystem::path canonicalCandidate = std::filesystem::weakly_canonical(candidate, ec);
				if (ec) {
					continue;
				}

				if (std::filesystem::exists(canonicalCandidate / "Bolt-Engine" / "src")
					&& std::filesystem::exists(canonicalCandidate / "Bolt-ScriptCore" / "Bolt-ScriptCore.csproj")) {
					return canonicalCandidate;
				}
			}

			return {};
		}

		std::string GetNativeLibraryFilename(const std::string& projectName) {
#if defined(_WIN32)
			return projectName + "-NativeScripts.dll";
#elif defined(__APPLE__)
			return "lib" + projectName + "-NativeScripts.dylib";
#else
			return "lib" + projectName + "-NativeScripts.so";
#endif
		}

		std::string GetNativeBuildArchitectureDirectory() {
#if defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
			return "x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
			return "arm64";
#else
			return {};
#endif
		}

		std::string GetNativeBuildPlatformDirectory() {
			const std::string architectureDirectory = GetNativeBuildArchitectureDirectory();
			if (architectureDirectory.empty()) {
				return {};
			}

#if defined(BT_PLATFORM_WINDOWS)
			return "windows-" + architectureDirectory;
#elif defined(BT_PLATFORM_LINUX)
			return "linux-" + architectureDirectory;
#else
			return {};
#endif
		}

		std::vector<std::string> GetPreferredBuildConfigurations() {
			std::vector<std::string> configurations;
			configurations.reserve(3);

			auto appendUnique = [&configurations](std::string value) {
				if (value.empty()) {
					return;
				}

				if (std::find(configurations.begin(), configurations.end(), value) == configurations.end()) {
					configurations.push_back(std::move(value));
				}
			};

			appendUnique(BoltProject::GetActiveBuildConfiguration());
			appendUnique("Release");
			appendUnique("Debug");
			appendUnique("Dist");
			return configurations;
		}

		std::vector<std::filesystem::path> BuildScriptCoreArtifactDirectories(const std::filesystem::path& engineRoot) {
			std::vector<std::filesystem::path> directories;
			if (engineRoot.empty()) {
				return directories;
			}

			auto appendUnique = [&directories](std::filesystem::path path) {
				if (path.empty()) {
					return;
				}

				std::error_code ec;
				const std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
				const std::filesystem::path key = ec ? path.lexically_normal() : normalized;
				if (std::find(directories.begin(), directories.end(), key) == directories.end()) {
					directories.push_back(key);
				}
			};

			const std::string platformDirectory = GetNativeBuildPlatformDirectory();
			const auto preferredConfigurations = GetPreferredBuildConfigurations();
			for (const std::string& config : preferredConfigurations) {
				if (!platformDirectory.empty()) {
					appendUnique(engineRoot / "bin" / (config + "-" + platformDirectory) / "Bolt-ScriptCore");
				}
			}

			const std::filesystem::path binDirectory = engineRoot / "bin";
			std::error_code ec;
			if (std::filesystem::exists(binDirectory, ec) && std::filesystem::is_directory(binDirectory, ec)) {
				for (const auto& entry : std::filesystem::directory_iterator(binDirectory, ec)) {
					if (ec || !entry.is_directory()) {
						continue;
					}

					appendUnique(entry.path() / "Bolt-ScriptCore");
				}
			}

			return directories;
		}

		std::filesystem::path FindScriptCoreArtifact(const std::filesystem::path& engineRoot, const std::string& filename) {
			for (const auto& directory : BuildScriptCoreArtifactDirectories(engineRoot)) {
				const std::filesystem::path candidate = directory / filename;
				if (std::filesystem::exists(candidate)) {
					return candidate;
				}
			}

			return {};
		}

		std::string BuildNativeScriptExportsSource() {
			return R"(#include <Scripting/NativeScript.hpp>

#if defined(_WIN32)
#define BOLT_NATIVE_SCRIPT_EXPORT extern "C" __declspec(dllexport)
#elif defined(__GNUC__)
#define BOLT_NATIVE_SCRIPT_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define BOLT_NATIVE_SCRIPT_EXPORT extern "C"
#endif

BOLT_NATIVE_SCRIPT_EXPORT void BoltInitialize(void* engineAPI) {
	Bolt::g_EngineAPI = static_cast<Bolt::NativeEngineAPI*>(engineAPI);
}

BOLT_NATIVE_SCRIPT_EXPORT Bolt::NativeScript* BoltCreateScript(const char* className) {
	return Bolt::NativeScriptRegistry::Create(className);
}

BOLT_NATIVE_SCRIPT_EXPORT int BoltHasScript(const char* className) {
	return Bolt::NativeScriptRegistry::Has(className) ? 1 : 0;
}

BOLT_NATIVE_SCRIPT_EXPORT void BoltDestroyScript(Bolt::NativeScript* script) {
	delete script;
}
)";
		}

		void EnsureNativeScriptExportsFile(const BoltProject& project) {
			if (project.NativeSourceDir.empty()) {
				return;
			}

			const std::filesystem::path sourceDirectory = project.NativeSourceDir;
			std::error_code ec;
			std::filesystem::create_directories(sourceDirectory, ec);

			const std::filesystem::path exportsPath = sourceDirectory / "NativeScriptExports.cpp";
			if (std::filesystem::exists(exportsPath, ec)) {
				return;
			}

			File::WriteAllText(exportsPath.string(), BuildNativeScriptExportsSource());
		}

		bool IsNativeScriptBootstrapFile(const std::filesystem::path& path) {
			return path.filename().string() == "NativeScriptExports.cpp";
		}

		bool IsNativeScriptSourceExtension(const std::filesystem::path& path) {
			std::string extension = path.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});

			return extension == ".c" || extension == ".cc" || extension == ".cpp" || extension == ".cxx";
		}

		bool HasNativeScriptSourceFiles(const std::filesystem::path& sourceDirectory) {
			std::error_code ec;
			if (sourceDirectory.empty() || !std::filesystem::exists(sourceDirectory, ec) || ec) {
				return false;
			}

			for (std::filesystem::recursive_directory_iterator it(sourceDirectory, std::filesystem::directory_options::skip_permission_denied, ec), end;
				 it != end;
				 it.increment(ec)) {
				if (ec) {
					ec.clear();
					continue;
				}

				if (!it->is_regular_file(ec) || ec) {
					ec.clear();
					continue;
				}

				const std::filesystem::path path = it->path();
				if (IsNativeScriptBootstrapFile(path)) {
					continue;
				}

				if (IsNativeScriptSourceExtension(path)) {
					return true;
				}
			}

			return false;
		}

		std::string BuildNativeScriptCMakeLists(const BoltProject& project) {
			const std::filesystem::path engineRoot = ResolveEngineRoot();
			const std::string nativeBoltRootFallback = engineRoot.empty()
				? std::string()
				: MakeRelativePathOrEmpty(std::filesystem::path(project.NativeScriptsDir), engineRoot);

			std::string boltRootBootstrap = R"(set(BOLT_DIR "$ENV{BOLT_DIR}" CACHE PATH "Path to the Bolt repository root")
)";
			if (!nativeBoltRootFallback.empty()) {
				boltRootBootstrap += "if(NOT BOLT_DIR)\n";
				boltRootBootstrap += "    get_filename_component(BOLT_DIR \"${CMAKE_CURRENT_LIST_DIR}/" + nativeBoltRootFallback + "\" ABSOLUTE)\n";
				boltRootBootstrap += "endif()\n";
			}
			boltRootBootstrap += R"(
if(NOT BOLT_DIR OR NOT EXISTS "${BOLT_DIR}/Bolt-Engine/src")
    message(FATAL_ERROR "Bolt engine sources not found. Set BOLT_DIR to the Bolt repository root before configuring native scripts.")
endif()

)";

			return R"(cmake_minimum_required(VERSION 3.20)
project()" + project.Name + R"(-NativeScripts LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Dist" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_FLAGS_DIST "${CMAKE_CXX_FLAGS_RELEASE}" CACHE STRING "Flags used by the C++ compiler for Dist builds." FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_DIST "${CMAKE_SHARED_LINKER_FLAGS_RELEASE}" CACHE STRING "Flags used by the shared linker for Dist builds." FORCE)

function(bt_normalize_native_arch out_var raw_arch)
    string(TOLOWER "${raw_arch}" raw_arch_lower)
    if(raw_arch_lower STREQUAL "amd64" OR raw_arch_lower STREQUAL "x86_64" OR raw_arch_lower STREQUAL "x64")
        set(${out_var} "x86_64" PARENT_SCOPE)
    elseif(raw_arch_lower STREQUAL "arm64" OR raw_arch_lower STREQUAL "aarch64")
        set(${out_var} "arm64" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "Unsupported native script architecture: ${raw_arch}")
    endif()
endfunction()

)" + boltRootBootstrap + R"(
file(GLOB_RECURSE NATIVE_SOURCES CONFIGURE_DEPENDS "Source/*.cpp" "Source/*.h" "Source/*.hpp")
add_library(${PROJECT_NAME} SHARED ${NATIVE_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    "${BOLT_DIR}/Bolt-Engine/src"
    "${BOLT_DIR}/External/spdlog/include"
    "${BOLT_DIR}/External/glm"
    "${BOLT_DIR}/External/entt/src"
    "${BOLT_DIR}/External/stb"
    "${BOLT_DIR}/External/magic_enum/include"
    "${BOLT_DIR}/External/cereal/include"
    "${BOLT_DIR}/External/glfw/include"
    "${BOLT_DIR}/External/glad/include"
    "${BOLT_DIR}/External/miniaudio"
    "${BOLT_DIR}/External/box2d/include"
    "${BOLT_DIR}/External/Bolt-Physics/include"
    "${BOLT_DIR}/External/dotnet"
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    BOLT_ALL_MODULES=1
    $<$<CONFIG:Debug>:BT_DEBUG;_DEBUG>
    $<$<CONFIG:Release>:BT_RELEASE;NDEBUG>
    $<$<CONFIG:Dist>:BT_DIST;NDEBUG>
)

if(WIN32)
    bt_normalize_native_arch(BT_NATIVE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    set(BT_NATIVE_PLATFORM "windows-${BT_NATIVE_ARCH}")
    target_compile_definitions(${PROJECT_NAME} PRIVATE BT_PLATFORM_WINDOWS)
elseif(UNIX AND NOT APPLE)
    bt_normalize_native_arch(BT_NATIVE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    set(BT_NATIVE_PLATFORM "linux-${BT_NATIVE_ARCH}")
    target_compile_definitions(${PROJECT_NAME} PRIVATE BT_PLATFORM_LINUX)
else()
    message(FATAL_ERROR "Unsupported platform for native scripts")
endif()

target_link_directories(${PROJECT_NAME} PRIVATE
    "$<$<CONFIG:Debug>:${BOLT_DIR}/bin/Debug-${BT_NATIVE_PLATFORM}/Bolt-Engine>"
    "$<$<CONFIG:Release>:${BOLT_DIR}/bin/Release-${BT_NATIVE_PLATFORM}/Bolt-Engine>"
    "$<$<CONFIG:Dist>:${BOLT_DIR}/bin/Dist-${BT_NATIVE_PLATFORM}/Bolt-Engine>"
)
target_link_libraries(${PROJECT_NAME} PRIVATE Bolt-Engine)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
endif()

set(NATIVE_OUTPUT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../bin")
foreach(config Debug Release Dist)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${NATIVE_OUTPUT_ROOT}/${config}-${BT_NATIVE_PLATFORM}/${PROJECT_NAME}"
        LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${NATIVE_OUTPUT_ROOT}/${config}-${BT_NATIVE_PLATFORM}/${PROJECT_NAME}"
    )
endforeach()
)";
		}

		void EnsureNativeScriptCMakeListsFile(const BoltProject& project) {
			if (project.NativeScriptsDir.empty()) {
				return;
			}

			std::error_code ec;
			std::filesystem::create_directories(project.NativeScriptsDir, ec);

			const std::filesystem::path cmakeListsPath = std::filesystem::path(project.NativeScriptsDir) / "CMakeLists.txt";
			if (std::filesystem::exists(cmakeListsPath, ec)) {
				return;
			}

			File::WriteAllText(cmakeListsPath.string(), BuildNativeScriptCMakeLists(project));
		}
	}

	static std::string GenerateGUID() {
		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

		auto hex = [&](int bytes) {
			std::stringstream ss;
			ss << std::hex << std::setfill('0');
			for (int i = 0; i < bytes; i++)
				ss << std::setw(2) << (dist(gen) & 0xFF);
			return ss.str();
		};

		return hex(4) + "-" + hex(2) + "-" + hex(2) + "-" + hex(2) + "-" + hex(6);
	}

	static std::string NowISO8601() {
		auto t = std::time(nullptr);
		std::tm tm{};
		if (!ToLocalTime(t, tm)) {
			return {};
		}
		std::stringstream ss;
		ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
		return ss.str();
	}

	static void ResolvePaths(BoltProject& p) {
		p.AssetsDirectory = Path::Combine(p.RootDirectory, "Assets");
		p.ScriptsDirectory = Path::Combine(p.AssetsDirectory, "Scripts");
		p.ScenesDirectory = Path::Combine(p.AssetsDirectory, "Scenes");
		p.BoltAssetsDirectory = Path::Combine(p.RootDirectory, "BoltAssets");
		p.NativeScriptsDir = Path::Combine(p.RootDirectory, "NativeScripts");
		p.NativeSourceDir = Path::Combine(p.NativeScriptsDir, "Source");
		p.PackagesDirectory = Path::Combine(p.RootDirectory, "Packages");
		p.CsprojPath = Path::Combine(p.RootDirectory, p.Name + ".csproj");
		p.SlnPath = Path::Combine(p.RootDirectory, p.Name + ".sln");
		p.ProjectFilePath = Path::Combine(p.RootDirectory, "bolt-project.json");
	}

	static Json::Value BuildProjectJson(const BoltProject& project) {
		Json::Value root = Json::Value::MakeObject();
		root.AddMember("name", project.Name);
		root.AddMember("engineVersion", project.EngineVersion);
		root.AddMember("startupScene", project.StartupScene);
		root.AddMember("lastOpenedScene", project.LastOpenedScene);
		root.AddMember("gameViewAspect", project.GameViewAspect);
		root.AddMember("gameViewVsync", project.GameViewVsync);
		root.AddMember("buildWidth", project.BuildWidth);
		root.AddMember("buildHeight", project.BuildHeight);
		root.AddMember("buildFullscreen", project.BuildFullscreen);
		root.AddMember("buildResizable", project.BuildResizable);
		if (!project.AppIconPath.empty()) {
			root.AddMember("appIcon", project.AppIconPath);
		}

		Json::Value buildScenes = Json::Value::MakeArray();
		for (const std::string& sceneName : project.BuildSceneList) {
			buildScenes.Append(sceneName);
		}
		root.AddMember("buildScenes", std::move(buildScenes));

		Json::Value globalSystems = Json::Value::MakeArray();
		for (const auto& registration : project.GlobalSystems) {
			if (registration.ClassName.empty()) {
				continue;
			}

			Json::Value systemValue = Json::Value::MakeObject();
			systemValue.AddMember("className", registration.ClassName);
			systemValue.AddMember("active", registration.Active);
			globalSystems.Append(std::move(systemValue));
		}
		root.AddMember("globalSystems", std::move(globalSystems));
		return root;
	}

	std::string BoltProject::GetUserAssemblyOutputPath(std::string_view configuration) const {
		const std::string resolvedConfiguration = configuration.empty()
			? GetActiveBuildConfiguration()
			: std::string(configuration);
		return Path::Combine(RootDirectory, "bin", resolvedConfiguration, Name + ".dll");
	}

	std::string BoltProject::GetActiveBuildConfiguration() {
		return BT_BUILD_CONFIG_NAME;
	}

	std::string BoltProject::GetActiveBuildDefineConstant() {
#if defined(BT_DEBUG)
		return "BT_DEBUG";
#elif defined(BT_RELEASE)
		return "BT_RELEASE";
#elif defined(BT_DIST)
		return "BT_DIST";
#else
		return {};
#endif
	}

	std::string BoltProject::BuildManagedDefineConstants(std::string_view primarySymbol) {
		std::string defineConstants;
		if (!primarySymbol.empty()) {
			defineConstants.assign(primarySymbol);
		}

		const std::string buildDefine = GetActiveBuildDefineConstant();
		if (!buildDefine.empty()) {
			if (!defineConstants.empty()) {
				defineConstants += "%3B";
			}
			defineConstants += buildDefine;
		}

		return defineConstants;
	}

	std::string BoltProject::GetNativeDllPath() const {
		const std::string libraryFilename = GetNativeLibraryFilename(Name);
		const std::string buildConfig = BT_BUILD_CONFIG_NAME;
		const std::string targetName = Name + "-NativeScripts";
		const std::string platformDirectory = GetNativeBuildPlatformDirectory();

		std::vector<std::filesystem::path> candidates;
		if (!platformDirectory.empty()) {
			candidates.emplace_back(std::filesystem::path(RootDirectory) / "bin" / (buildConfig + "-" + platformDirectory) / targetName / libraryFilename);
		}

		candidates.emplace_back(std::filesystem::path(NativeScriptsDir) / "build" / buildConfig / libraryFilename);
		candidates.emplace_back(std::filesystem::path(NativeScriptsDir) / "build" / libraryFilename);
		candidates.emplace_back(std::filesystem::path(NativeScriptsDir) / "build" / "Release" / libraryFilename);
		candidates.emplace_back(std::filesystem::path(NativeScriptsDir) / "build" / "Debug" / libraryFilename);
		candidates.emplace_back(std::filesystem::path(NativeScriptsDir) / "build" / "Dist" / libraryFilename);

		for (const auto& candidate : candidates) {
			if (std::filesystem::exists(candidate)) {
				auto normalizedCandidate = candidate;
				return normalizedCandidate.make_preferred().string();
			}
		}

		auto fallbackCandidate = candidates.front();
		return fallbackCandidate.make_preferred().string();
	}

	std::string BoltProject::GetSceneFilePath(const std::string& sceneName) const {
		return Path::Combine(ScenesDirectory, sceneName + ".scene");
	}

	void BoltProject::EnsureNativeScriptBootstrapFiles() const {
		EnsureNativeScriptExportsFile(*this);
	}

	void BoltProject::EnsureNativeScriptProjectFiles() const {
		EnsureNativeScriptCMakeListsFile(*this);
		EnsureNativeScriptExportsFile(*this);
	}

	bool BoltProject::HasNativeScriptSources() const {
		return HasNativeScriptSourceFiles(std::filesystem::path(NativeSourceDir));
	}

	void BoltProject::Save() const {
		File::WriteAllText(ProjectFilePath, Json::Stringify(BuildProjectJson(*this), true));
	}

	std::string BoltProject::GetDefaultProjectsDir() {
		return Path::Combine(Path::GetSpecialFolderPath(SpecialFolder::User), "Bolt", "Projects");
	}

	bool BoltProject::IsValidProjectName(const std::string& name) {
		if (name.empty() || name.size() > 64) return false;
		if (name[0] == '.') return false;
		for (char c : name)
			if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*')
				return false;
		return true;
	}

	bool BoltProject::Validate(const std::string& rootDir) {
		return std::filesystem::exists(Path::Combine(rootDir, "bolt-project.json"))
			&& std::filesystem::exists(Path::Combine(rootDir, "Assets"));
	}

	BoltProject BoltProject::Load(const std::string& rootDir) {
		BoltProject project;
		project.RootDirectory = rootDir;

		std::string configPath = Path::Combine(rootDir, "bolt-project.json");
		if (File::Exists(configPath)) {
			std::string jsonText = File::ReadAllText(configPath);
			Json::Value root;
			std::string parseError;
			if (!jsonText.empty() && Json::TryParse(jsonText, root, &parseError) && root.IsObject()) {
				if (const Json::Value* nameValue = root.FindMember("name")) {
					project.Name = nameValue->AsStringOr();
				}
				if (const Json::Value* versionValue = root.FindMember("engineVersion")) {
					project.EngineVersion = versionValue->AsStringOr();
				}
				if (const Json::Value* startupValue = root.FindMember("startupScene")) {
					const std::string startupScene = startupValue->AsStringOr();
					if (!startupScene.empty()) {
						project.StartupScene = startupScene;
					}
				}
				if (const Json::Value* lastSceneValue = root.FindMember("lastOpenedScene")) {
					const std::string lastScene = lastSceneValue->AsStringOr();
					if (!lastScene.empty()) {
						project.LastOpenedScene = lastScene;
					}
				}
				if (const Json::Value* gameViewAspectValue = root.FindMember("gameViewAspect")) {
					const std::string gameViewAspect = gameViewAspectValue->AsStringOr();
					if (!gameViewAspect.empty()) {
						project.GameViewAspect = gameViewAspect;
					}
				}
				if (const Json::Value* gameViewVsyncValue = root.FindMember("gameViewVsync")) {
					project.GameViewVsync = gameViewVsyncValue->AsBoolOr(true);
				}
				if (const Json::Value* buildWidthValue = root.FindMember("buildWidth")) {
					project.BuildWidth = buildWidthValue->AsIntOr(1280);
				}
				if (const Json::Value* buildHeightValue = root.FindMember("buildHeight")) {
					project.BuildHeight = buildHeightValue->AsIntOr(720);
				}
				if (const Json::Value* fullscreenValue = root.FindMember("buildFullscreen")) {
					project.BuildFullscreen = fullscreenValue->AsBoolOr(false);
				}
				if (const Json::Value* resizableValue = root.FindMember("buildResizable")) {
					project.BuildResizable = resizableValue->AsBoolOr(true);
				}
				if (const Json::Value* iconValue = root.FindMember("appIcon")) {
					project.AppIconPath = iconValue->AsStringOr();
				}
				if (const Json::Value* buildScenesValue = root.FindMember("buildScenes")) {
					project.BuildSceneList.clear();
					for (const Json::Value& sceneValue : buildScenesValue->GetArray()) {
						const std::string sceneName = sceneValue.AsStringOr();
						if (!sceneName.empty()) {
							project.BuildSceneList.push_back(sceneName);
						}
					}
				}
				if (const Json::Value* globalSystemsValue = root.FindMember("globalSystems")) {
					project.GlobalSystems.clear();
					for (const Json::Value& systemValue : globalSystemsValue->GetArray()) {
						if (systemValue.IsString()) {
							const std::string className = systemValue.AsStringOr();
							if (!className.empty()) {
								project.GlobalSystems.push_back({ className, true });
							}
							continue;
						}

						if (!systemValue.IsObject()) {
							continue;
						}

						const Json::Value* classNameValue = systemValue.FindMember("className");
						const std::string className = classNameValue ? classNameValue->AsStringOr() : "";
						if (className.empty()) {
							continue;
						}

						const Json::Value* activeValue = systemValue.FindMember("active");
						project.GlobalSystems.push_back({ className, activeValue ? activeValue->AsBoolOr(true) : true });
					}
				}
			}
			else if (!jsonText.empty()) {
				BT_CORE_WARN_TAG("BoltProject", "Failed to parse '{}': {}", configPath, parseError);
			}
		}

		if (project.Name.empty())
			project.Name = std::filesystem::path(rootDir).filename().string();
		if (project.EngineVersion.empty())
			project.EngineVersion = BT_VERSION;

		ResolvePaths(project);
		return project;
	}

	void CreateDirectoryTree(const BoltProject& project) {
		Directory::Create(project.RootDirectory);
		Directory::Create(project.AssetsDirectory);
		Directory::Create(project.ScriptsDirectory);
		Directory::Create(project.ScenesDirectory);
		Directory::Create(project.BoltAssetsDirectory);
		Directory::Create(project.NativeScriptsDir);
		Directory::Create(project.NativeSourceDir);
		Directory::Create(project.PackagesDirectory);
		Directory::Create(Path::Combine(project.RootDirectory, "bin"));
		Directory::Create(Path::Combine(project.RootDirectory, ".vscode"));
	}

	void ConfigurePackages(const BoltProject& project) {

		const std::filesystem::path engineRoot = ResolveEngineRoot();
		const std::filesystem::path scriptCoreOutput = FindScriptCoreArtifact(engineRoot, "Bolt-ScriptCore.dll");
		const std::filesystem::path scriptCorePdb = FindScriptCoreArtifact(engineRoot, "Bolt-ScriptCore.pdb");

		std::filesystem::path packageDir = std::filesystem::path(project.PackagesDirectory) / "Bolt-ScriptCore";
		std::filesystem::create_directories(packageDir);

		if (std::filesystem::exists(scriptCoreOutput))
			std::filesystem::copy_file(scriptCoreOutput, packageDir / "Bolt-ScriptCore.dll", std::filesystem::copy_options::overwrite_existing);

		if (std::filesystem::exists(scriptCorePdb))
			std::filesystem::copy_file(scriptCorePdb, packageDir / "Bolt-ScriptCore.pdb", std::filesystem::copy_options::overwrite_existing);
	}

	BoltProject BoltProject::Create(const std::string& name, const std::string& parentDir,
		const CreateProgressCallback& progressCallback) {
		BoltProject project;
		project.Name = name;
		project.RootDirectory = Path::Combine(parentDir, name);
		project.EngineVersion = BT_VERSION;
		ResolvePaths(project);

		ReportCreateProgress(progressCallback, 0.08f, "Creating project folders...");
		CreateDirectoryTree(project);
		ReportCreateProgress(progressCallback, 0.18f, "Configuring engine packages...");
		ConfigurePackages(project);

		std::string engineAssets = Path::ResolveBoltAssets("");
		if (!engineAssets.empty() && Directory::Exists(engineAssets)) {
			ReportCreateProgress(progressCallback, 0.34f, "Copying Bolt assets...");
			try {
				std::filesystem::copy(engineAssets, project.BoltAssetsDirectory,
					std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing);
			}
			catch (...) {}
		}

		if (project.BuildSceneList.empty()) {
			project.BuildSceneList.push_back(project.StartupScene);
		}

		// Write bolt-project.json
		{
			ReportCreateProgress(progressCallback, 0.48f, "Writing project configuration...");
			Json::Value root = BuildProjectJson(project);
			root.AddMember("createdAt", NowISO8601());
			File::WriteAllText(project.ProjectFilePath, Json::Stringify(root, true));
		}

		// Generate starter scene
		{
			ReportCreateProgress(progressCallback, 0.56f, "Generating starter scene...");
			Json::Value sceneRoot = Json::Value::MakeObject();
			sceneRoot.AddMember("version", 1);
			sceneRoot.AddMember("name", project.StartupScene);
			sceneRoot.AddMember("systems", Json::Value::MakeArray());
			sceneRoot.AddMember("entities", Json::Value::MakeArray());
			File::WriteAllText(project.GetSceneFilePath(project.StartupScene), Json::Stringify(sceneRoot, true));
		}

		// Preserve existing NuGet PackageReference entries if .csproj already exists
		std::string existingPackageRefs;
		if (File::Exists(project.CsprojPath)) {
			std::ifstream existing(project.CsprojPath);
			std::string content((std::istreambuf_iterator<char>(existing)), std::istreambuf_iterator<char>());
			existing.close();

			// Extract all ItemGroup blocks that contain PackageReference
			size_t searchPos = 0;
			while (searchPos < content.size()) {
				size_t igStart = content.find("<ItemGroup>", searchPos);
				if (igStart == std::string::npos) break;
				size_t igEnd = content.find("</ItemGroup>", igStart);
				if (igEnd == std::string::npos) break;
				igEnd += 12; // length of "</ItemGroup>"

				std::string block = content.substr(igStart, igEnd - igStart);
				const bool referencesScriptCore = block.find("Bolt-ScriptCore") != std::string::npos;
				if (block.find("PackageReference") != std::string::npos
					|| (block.find("<Reference ") != std::string::npos && !referencesScriptCore)
					|| (block.find("ProjectReference") != std::string::npos && !referencesScriptCore)) {
					existingPackageRefs += "  " + block + "\n";
				}
				searchPos = igEnd;
			}
		}

		ReportCreateProgress(progressCallback, 0.66f, "Generating C# project files...");
		std::string csproj = R"CS(<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Library</OutputType>
    <TargetFramework>net9.0</TargetFramework>
    <Configurations>Debug;Release;Dist</Configurations>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <EnableDynamicLoading>true</EnableDynamicLoading>
    <CopyLocalLockFileAssemblies>true</CopyLocalLockFileAssemblies>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <EnableDefaultNoneItems>false</EnableDefaultNoneItems>
    <Nullable>enable</Nullable>
    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>
  </PropertyGroup>

  <PropertyGroup Condition="'$(Configuration)' == 'Debug'">
    <OutputPath>bin\Debug\</OutputPath>
  </PropertyGroup>

  <PropertyGroup Condition="'$(Configuration)' == 'Release'">
    <OutputPath>bin\Release\</OutputPath>
    <Optimize>true</Optimize>
  </PropertyGroup>

  <PropertyGroup Condition="'$(Configuration)' == 'Dist'">
    <OutputPath>bin\Dist\</OutputPath>
    <Optimize>true</Optimize>
  </PropertyGroup>

  <ItemGroup>
    <Compile Include="Assets\Scripts\**\*.cs" />
  </ItemGroup>

  <ItemGroup>
    <Reference Include="Bolt-ScriptCore">
      <HintPath>Packages\Bolt-ScriptCore\Bolt-ScriptCore.dll</HintPath>
      <Private>true</Private>
    </Reference>
  </ItemGroup>
)CS" + existingPackageRefs + R"CS(
</Project>
)CS";
		File::WriteAllText(project.CsprojPath, csproj);

		// Generate .sln — only the user project is visible.
		// Bolt-ScriptCore is linked via DLL reference in the .csproj,
		// so it does not need a project entry here.
		ReportCreateProgress(progressCallback, 0.72f, "Generating solution and starter scripts...");
		std::string projGuid = GenerateGUID();

		std::string sln = R"(
Microsoft Visual Studio Solution File, Format Version 12.00
# Visual Studio Version 17
Project("{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}") = ")" + name + R"(", ")" + name + R"(.csproj", "{)" + projGuid + R"(}"
EndProject
Global
	GlobalSection(SolutionConfigurationPlatforms) = preSolution
		Debug|Any CPU = Debug|Any CPU
		Release|Any CPU = Release|Any CPU
		Dist|Any CPU = Dist|Any CPU
	EndGlobalSection
	GlobalSection(ProjectConfigurationPlatforms) = postSolution
		{)" + projGuid + R"(}.Debug|Any CPU.ActiveCfg = Debug|AnyCPU
		{)" + projGuid + R"(}.Debug|Any CPU.Build.0 = Debug|AnyCPU
		{)" + projGuid + R"(}.Release|Any CPU.ActiveCfg = Release|AnyCPU
		{)" + projGuid + R"(}.Release|Any CPU.Build.0 = Release|AnyCPU
		{)" + projGuid + R"(}.Dist|Any CPU.ActiveCfg = Dist|AnyCPU
		{)" + projGuid + R"(}.Dist|Any CPU.Build.0 = Dist|AnyCPU
	EndGlobalSection
	GlobalSection(SolutionProperties) = preSolution
		HideSolutionNode = FALSE
	EndGlobalSection
EndGlobal
)";
		File::WriteAllText(project.SlnPath, sln);

		// Generate starter script
		std::string starterScript = R"(using Bolt;

public class GameScript : EntityScript
{
    public override void OnStart()
    {
        Log.Info("Hello from )" + name + R"(!");
    }

    public override void OnUpdate()
    {
    }
}
)";
		File::WriteAllText(Path::Combine(project.ScriptsDirectory, "GameScript.cs"), starterScript);

		// Generate .vscode/settings.json
		std::string vsCodeSettings = R"({
    "omnisharp.projectFilesExcludePattern": [],
    "dotnet.defaultSolution": ")" + name + R"(.sln"
})";
		File::WriteAllText(Path::Combine(project.RootDirectory, ".vscode", "settings.json"), vsCodeSettings);

		project.EnsureNativeScriptProjectFiles();

		// Generate .gitignore
		File::WriteAllText(Path::Combine(project.RootDirectory, ".gitignore"),
			"bin/\nobj/\n.vs/\n*.user\nNativeScripts/build/\nPackages/\n");

		ReportCreateProgress(progressCallback, 0.82f, "Project files ready.");
		BT_INFO_TAG("BoltProject", "Created project: {} at {}", name, project.RootDirectory);
		return project;
	}

} // namespace Bolt
