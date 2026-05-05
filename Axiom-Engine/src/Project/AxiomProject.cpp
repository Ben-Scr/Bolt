#include "pch.hpp"
#include "Project/AxiomProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/Directory.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Utils/Process.hpp"
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

namespace Axiom {
	namespace {
		void ReportCreateProgress(const AxiomProject::CreateProgressCallback& callback, float progress, std::string_view stage) {
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

				if (std::filesystem::exists(canonicalCandidate / "Axiom-Engine" / "src")
					&& std::filesystem::exists(canonicalCandidate / "Axiom-ScriptCore" / "Axiom-ScriptCore.csproj")) {
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

#if defined(AIM_PLATFORM_WINDOWS)
			return "windows-" + architectureDirectory;
#elif defined(AIM_PLATFORM_LINUX)
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

			appendUnique(AxiomProject::GetActiveBuildConfiguration());
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
					appendUnique(engineRoot / "bin" / (config + "-" + platformDirectory) / "Axiom-ScriptCore");
				}
			}

			const std::filesystem::path binDirectory = engineRoot / "bin";
			std::error_code ec;
			if (std::filesystem::exists(binDirectory, ec) && std::filesystem::is_directory(binDirectory, ec)) {
				for (const auto& entry : std::filesystem::directory_iterator(binDirectory, ec)) {
					if (ec || !entry.is_directory()) {
						continue;
					}

					appendUnique(entry.path() / "Axiom-ScriptCore");
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
#define AXIOM_NATIVE_SCRIPT_EXPORT extern "C" __declspec(dllexport)
#elif defined(__GNUC__)
#define AXIOM_NATIVE_SCRIPT_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define AXIOM_NATIVE_SCRIPT_EXPORT extern "C"
#endif

AXIOM_NATIVE_SCRIPT_EXPORT void AxiomInitialize(void* engineAPI) {
	Axiom::g_EngineAPI = static_cast<Axiom::NativeEngineAPI*>(engineAPI);
}

AXIOM_NATIVE_SCRIPT_EXPORT Axiom::NativeScript* AxiomCreateScript(const char* className) {
	return Axiom::NativeScriptRegistry::Create(className);
}

AXIOM_NATIVE_SCRIPT_EXPORT int AxiomHasScript(const char* className) {
	return Axiom::NativeScriptRegistry::Has(className) ? 1 : 0;
}

AXIOM_NATIVE_SCRIPT_EXPORT void AxiomDestroyScript(Axiom::NativeScript* script) {
	delete script;
}
)";
		}

		void EnsureNativeScriptExportsFile(const AxiomProject& project) {
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

		std::string BuildNativeScriptCMakeLists(const AxiomProject& project) {
			const std::filesystem::path engineRoot = ResolveEngineRoot();
			const std::string nativeAxiomRootFallback = engineRoot.empty()
				? std::string()
				: MakeRelativePathOrEmpty(std::filesystem::path(project.NativeScriptsDir), engineRoot);

			// Empty when no custom defines — the placeholder substitution drops the line entirely.
			std::string customDefinesBlock;
			if (!project.CustomDefines.empty()) {
				customDefinesBlock = "target_compile_definitions(${PROJECT_NAME} PRIVATE";
				for (const std::string& d : project.CustomDefines) {
					if (d.empty()) continue;
					customDefinesBlock += " ";
					customDefinesBlock += d;
				}
				customDefinesBlock += ")";
			}

			std::string axiomRootBootstrap = R"(set(AXIOM_DIR "$ENV{AXIOM_DIR}" CACHE PATH "Path to the Axiom repository root")
)";
			if (!nativeAxiomRootFallback.empty()) {
				axiomRootBootstrap += "if(NOT AXIOM_DIR)\n";
				axiomRootBootstrap += "    get_filename_component(AXIOM_DIR \"${CMAKE_CURRENT_LIST_DIR}/" + nativeAxiomRootFallback + "\" ABSOLUTE)\n";
				axiomRootBootstrap += "endif()\n";
			}
			axiomRootBootstrap += R"(
if(NOT AXIOM_DIR OR NOT EXISTS "${AXIOM_DIR}/Axiom-Engine/src")
    message(FATAL_ERROR "Axiom engine sources not found. Set AXIOM_DIR to the Axiom repository root before configuring native scripts.")
endif()

)";

			std::string output = R"(cmake_minimum_required(VERSION 3.20)
project()" + project.Name + R"(-NativeScripts LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Dist" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_FLAGS_DIST "${CMAKE_CXX_FLAGS_RELEASE}" CACHE STRING "Flags used by the C++ compiler for Dist builds." FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_DIST "${CMAKE_SHARED_LINKER_FLAGS_RELEASE}" CACHE STRING "Flags used by the shared linker for Dist builds." FORCE)

function(aim_normalize_native_arch out_var raw_arch)
    string(TOLOWER "${raw_arch}" raw_arch_lower)
    if(raw_arch_lower STREQUAL "amd64" OR raw_arch_lower STREQUAL "x86_64" OR raw_arch_lower STREQUAL "x64")
        set(${out_var} "x86_64" PARENT_SCOPE)
    elseif(raw_arch_lower STREQUAL "arm64" OR raw_arch_lower STREQUAL "aarch64")
        set(${out_var} "arm64" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "Unsupported native script architecture: ${raw_arch}")
    endif()
endfunction()

)" + axiomRootBootstrap + R"(
file(GLOB_RECURSE NATIVE_SOURCES CONFIGURE_DEPENDS "Source/*.cpp" "Source/*.h" "Source/*.hpp")
add_library(${PROJECT_NAME} SHARED ${NATIVE_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    "${AXIOM_DIR}/Axiom-Engine/src"
    "${AXIOM_DIR}/External/spdlog/include"
    "${AXIOM_DIR}/External/glm"
    "${AXIOM_DIR}/External/entt/src"
    "${AXIOM_DIR}/External/stb"
    "${AXIOM_DIR}/External/magic_enum/include"
    "${AXIOM_DIR}/External/cereal/include"
    "${AXIOM_DIR}/External/glfw/include"
    "${AXIOM_DIR}/External/glad/include"
    "${AXIOM_DIR}/External/miniaudio"
    "${AXIOM_DIR}/External/box2d/include"
    "${AXIOM_DIR}/External/Axiom-Physics/include"
    "${AXIOM_DIR}/External/dotnet"
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    AXIOM_ALL_MODULES=1
    $<$<CONFIG:Debug>:AIM_DEBUG;_DEBUG>
    $<$<CONFIG:Release>:AIM_RELEASE;NDEBUG>
    $<$<CONFIG:Dist>:AIM_DIST;NDEBUG>
)

# AXIOM_BUILD_DEVELOPMENT/RELEASE — mirrors the C# define so native scripts can use #ifdef.
if(NOT DEFINED AXIOM_BUILD_PROFILE)
    set(AXIOM_BUILD_PROFILE "DEVELOPMENT")
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE AXIOM_BUILD_${AXIOM_BUILD_PROFILE})

# Project custom defines — same list as C# <DefineConstants>, baked at save time.
)" + std::string(R"(@AXIOM_PROJECT_CUSTOM_DEFINES_BLOCK@)") + R"(

if(WIN32)
    aim_normalize_native_arch(AIM_NATIVE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    set(AIM_NATIVE_PLATFORM "windows-${AIM_NATIVE_ARCH}")
    target_compile_definitions(${PROJECT_NAME} PRIVATE AIM_PLATFORM_WINDOWS)
elseif(UNIX AND NOT APPLE)
    aim_normalize_native_arch(AIM_NATIVE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    set(AIM_NATIVE_PLATFORM "linux-${AIM_NATIVE_ARCH}")
    target_compile_definitions(${PROJECT_NAME} PRIVATE AIM_PLATFORM_LINUX)
else()
    message(FATAL_ERROR "Unsupported platform for native scripts")
endif()

target_link_directories(${PROJECT_NAME} PRIVATE
    "$<$<CONFIG:Debug>:${AXIOM_DIR}/bin/Debug-${AIM_NATIVE_PLATFORM}/Axiom-Engine>"
    "$<$<CONFIG:Release>:${AXIOM_DIR}/bin/Release-${AIM_NATIVE_PLATFORM}/Axiom-Engine>"
    "$<$<CONFIG:Dist>:${AXIOM_DIR}/bin/Dist-${AIM_NATIVE_PLATFORM}/Axiom-Engine>"
)
target_link_libraries(${PROJECT_NAME} PRIVATE Axiom-Engine)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
endif()

set(NATIVE_OUTPUT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../bin")
foreach(config Debug Release Dist)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${NATIVE_OUTPUT_ROOT}/${config}-${AIM_NATIVE_PLATFORM}/${PROJECT_NAME}"
        LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${NATIVE_OUTPUT_ROOT}/${config}-${AIM_NATIVE_PLATFORM}/${PROJECT_NAME}"
    )
endforeach()
)";

			// Substitute placeholder; strip the line entirely when no defines were configured.
			const std::string placeholder = "@AXIOM_PROJECT_CUSTOM_DEFINES_BLOCK@";
			const auto pos = output.find(placeholder);
			if (pos != std::string::npos) {
				if (customDefinesBlock.empty()) {
					std::size_t lineEnd = output.find('\n', pos);
					if (lineEnd == std::string::npos) lineEnd = output.size();
					else lineEnd += 1;
					output.erase(pos, lineEnd - pos);
				} else {
					output.replace(pos, placeholder.size(), customDefinesBlock);
				}
			}
			return output;
		}

		void EnsureNativeScriptCMakeListsFile(const AxiomProject& project) {
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

	static void ResolvePaths(AxiomProject& p) {
		p.AssetsDirectory = Path::Combine(p.RootDirectory, "Assets");
		p.ScriptsDirectory = Path::Combine(p.AssetsDirectory, "Scripts");
		p.ScenesDirectory = Path::Combine(p.AssetsDirectory, "Scenes");
		p.AxiomAssetsDirectory = Path::Combine(p.RootDirectory, "AxiomAssets");
		p.NativeScriptsDir = Path::Combine(p.RootDirectory, "NativeScripts");
		p.NativeSourceDir = Path::Combine(p.NativeScriptsDir, "Source");
		p.PackagesDirectory = Path::Combine(p.RootDirectory, "Packages");
		p.CsprojPath = Path::Combine(p.RootDirectory, p.Name + ".csproj");
		p.SlnPath = Path::Combine(p.RootDirectory, p.Name + ".sln");
		p.ProjectFilePath = Path::Combine(p.RootDirectory, "axiom-project.json");
	}

	static Json::Value BuildProjectJson(const AxiomProject& project) {
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

		// Missing field == legacy "scan everything" default; emit only when allow-list is set.
		if (!project.Packages.empty()) {
			Json::Value packages = Json::Value::MakeArray();
			for (const std::string& name : project.Packages) {
				if (!name.empty()) {
					packages.Append(name);
				}
			}
			root.AddMember("packages", std::move(packages));
		}

		// Only written when non-default to keep virgin project files clean.
		const auto& prof = project.Profiler;
		const bool profilerNonDefault =
			prof.EnableInRuntime ||
			prof.TrackInBackground ||
			prof.SamplingHz != 60 ||
			prof.TrackingSpan != 200 ||
			!prof.ModuleEnabled.empty();
		if (profilerNonDefault) {
			Json::Value profilerJson = Json::Value::MakeObject();
			profilerJson.AddMember("enableInRuntime", prof.EnableInRuntime);
			profilerJson.AddMember("trackInBackground", prof.TrackInBackground);
			profilerJson.AddMember("samplingHz", prof.SamplingHz);
			profilerJson.AddMember("trackingSpan", prof.TrackingSpan);
			if (!prof.ModuleEnabled.empty()) {
				Json::Value modulesJson = Json::Value::MakeObject();
				for (const auto& [name, enabled] : prof.ModuleEnabled) {
					modulesJson.AddMember(name, enabled);
				}
				profilerJson.AddMember("modules", std::move(modulesJson));
			}
			root.AddMember("profiler", std::move(profilerJson));
		}

		if (!project.ShowRuntimeStats) {
			root.AddMember("showRuntimeStats", false);
		}
		if (!project.ShowRuntimeLogs) {
			root.AddMember("showRuntimeLogs", false);
		}

		if (project.ActiveBuildProfile != AxiomProject::BuildProfile::Development) {
			root.AddMember("buildProfile",
				std::string(AxiomProject::BuildProfileToString(project.ActiveBuildProfile)));
		}
		if (!project.CustomDefines.empty()) {
			Json::Value definesJson = Json::Value::MakeArray();
			for (const std::string& d : project.CustomDefines) {
				if (!d.empty()) definesJson.Append(Json::Value(d));
			}
			root.AddMember("customDefines", std::move(definesJson));
		}

		return root;
	}

	std::string AxiomProject::GetUserAssemblyOutputPath(std::string_view configuration) const {
		const std::string resolvedConfiguration = configuration.empty()
			? GetActiveBuildConfiguration()
			: std::string(configuration);
		return Path::Combine(RootDirectory, "bin", resolvedConfiguration, Name + ".dll");
	}

	std::string AxiomProject::GetActiveBuildConfiguration() {
		return AIM_BUILD_CONFIG_NAME;
	}

	std::string AxiomProject::GetActiveBuildDefineConstant() {
#if defined(AIM_DEBUG)
		return "AIM_DEBUG";
#elif defined(AIM_RELEASE)
		return "AIM_RELEASE";
#elif defined(AIM_DIST)
		return "AIM_DIST";
#else
		return {};
#endif
	}

	std::string AxiomProject::BuildManagedDefineConstants(std::string_view primarySymbol) {
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

		// Editor hot-reload always uses Development; shipping defaults shouldn't bleed into editor preview.
		AxiomProject* project = ProjectManager::GetCurrentProject();
		const std::string profileDefine = (primarySymbol == "AXIOM_EDITOR")
			? std::string("AXIOM_BUILD_DEVELOPMENT")
			: (project ? project->GetActiveBuildProfileDefine() : std::string("AXIOM_BUILD_DEVELOPMENT"));
		if (!profileDefine.empty()) {
			if (!defineConstants.empty()) defineConstants += "%3B";
			defineConstants += profileDefine;
		}
		if (project) {
			for (const std::string& custom : project->CustomDefines) {
				if (custom.empty()) continue;
				if (!defineConstants.empty()) defineConstants += "%3B";
				defineConstants += custom;
			}
		}

		return defineConstants;
	}

	std::string AxiomProject::GetActiveBuildProfileDefine() const {
		switch (ActiveBuildProfile) {
			case BuildProfile::Release:     return "AXIOM_BUILD_RELEASE";
			case BuildProfile::Development: return "AXIOM_BUILD_DEVELOPMENT";
		}
		return "AXIOM_BUILD_DEVELOPMENT";
	}

	const char* AxiomProject::BuildProfileToString(BuildProfile profile) {
		switch (profile) {
			case BuildProfile::Release:     return "Release";
			case BuildProfile::Development: return "Development";
		}
		return "Development";
	}

	AxiomProject::BuildProfile AxiomProject::BuildProfileFromString(std::string_view value) {
		if (value == "Release") return BuildProfile::Release;
		return BuildProfile::Development;
	}

	std::string AxiomProject::GetNativeDllPath() const {
		const std::string libraryFilename = GetNativeLibraryFilename(Name);
		const std::string buildConfig = AIM_BUILD_CONFIG_NAME;
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

	std::string AxiomProject::GetSceneFilePath(const std::string& sceneName) const {
		const std::filesystem::path assetsRoot(AssetsDirectory);
		const std::string sceneFileName = sceneName + ".scene";
		std::error_code ec;
		if (std::filesystem::exists(assetsRoot, ec) && !ec) {
			for (std::filesystem::recursive_directory_iterator it(
					 assetsRoot,
					 std::filesystem::directory_options::skip_permission_denied,
					 ec),
				 end;
				 it != end;
				 it.increment(ec)) {
				if (ec) {
					ec.clear();
					continue;
				}

				if (it->is_regular_file(ec) && !ec && it->path().filename().string() == sceneFileName) {
					auto path = it->path();
					return path.make_preferred().string();
				}
			}
		}

		return Path::Combine(ScenesDirectory, sceneFileName);
	}

	void AxiomProject::EnsureNativeScriptBootstrapFiles() const {
		EnsureNativeScriptExportsFile(*this);
	}

	void AxiomProject::EnsureNativeScriptProjectFiles() const {
		EnsureNativeScriptCMakeListsFile(*this);
		EnsureNativeScriptExportsFile(*this);
	}

	bool AxiomProject::HasNativeScriptSources() const {
		return HasNativeScriptSourceFiles(std::filesystem::path(NativeSourceDir));
	}

	void AxiomProject::Save() const {
		File::WriteAllText(ProjectFilePath, Json::Stringify(BuildProjectJson(*this), true));
	}

	std::string AxiomProject::GetDefaultProjectsDir() {
		return Path::Combine(Path::GetSpecialFolderPath(SpecialFolder::User), "Axiom", "Projects");
	}

	bool AxiomProject::IsValidProjectName(const std::string& name) {
		if (name.empty() || name.size() > 64) return false;
		if (name[0] == '.') return false;
		for (char c : name)
			if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*')
				return false;
		return true;
	}

	bool AxiomProject::Validate(const std::string& rootDir) {
		return std::filesystem::exists(Path::Combine(rootDir, "axiom-project.json"))
			&& std::filesystem::exists(Path::Combine(rootDir, "Assets"));
	}

	AxiomProject AxiomProject::Load(const std::string& rootDir) {
		AxiomProject project;
		project.RootDirectory = rootDir;

		std::string configPath = Path::Combine(rootDir, "axiom-project.json");
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
				if (const Json::Value* packagesValue = root.FindMember("packages")) {
					project.Packages.clear();
					// Skip + warn on malformed input rather than asserting in Json::Value::GetArray.
					if (packagesValue->IsArray()) {
						for (const Json::Value& packageValue : packagesValue->GetArray()) {
							const std::string packageName = packageValue.AsStringOr();
							if (!packageName.empty()) {
								project.Packages.push_back(packageName);
							}
						}
					}
					else {
						AIM_CORE_WARN_TAG("AxiomProject",
							"Project '{}': 'packages' field is not a JSON array; ignoring.", configPath);
					}
				}
				if (const Json::Value* profilerValue = root.FindMember("profiler")) {
					if (const Json::Value* v = profilerValue->FindMember("enableInRuntime"))
						project.Profiler.EnableInRuntime = v->AsBoolOr(false);
					if (const Json::Value* v = profilerValue->FindMember("trackInBackground"))
						project.Profiler.TrackInBackground = v->AsBoolOr(false);
					if (const Json::Value* v = profilerValue->FindMember("samplingHz"))
						project.Profiler.SamplingHz = static_cast<int>(v->AsInt64Or(60));
					if (const Json::Value* v = profilerValue->FindMember("trackingSpan"))
						project.Profiler.TrackingSpan = static_cast<int>(v->AsInt64Or(200));
					if (const Json::Value* modulesValue = profilerValue->FindMember("modules")) {
						project.Profiler.ModuleEnabled.clear();
						for (const auto& [name, value] : modulesValue->GetObject()) {
							project.Profiler.ModuleEnabled.emplace_back(
								std::string(name), value.AsBoolOr(true));
						}
					}
				}
				if (const Json::Value* v = root.FindMember("showRuntimeStats"))
					project.ShowRuntimeStats = v->AsBoolOr(true);
				if (const Json::Value* v = root.FindMember("showRuntimeLogs"))
					project.ShowRuntimeLogs = v->AsBoolOr(true);
				if (const Json::Value* v = root.FindMember("buildProfile"))
					project.ActiveBuildProfile = AxiomProject::BuildProfileFromString(v->AsStringOr("Development"));
				if (const Json::Value* v = root.FindMember("customDefines"); v && v->IsArray()) {
					project.CustomDefines.clear();
					for (const Json::Value& item : v->GetArray()) {
						if (item.IsString()) {
							std::string s = item.AsStringOr();
							if (!s.empty()) project.CustomDefines.push_back(std::move(s));
						}
					}
				}
			}
			else if (!jsonText.empty()) {
				AIM_CORE_WARN_TAG("AxiomProject", "Failed to parse '{}': {}", configPath, parseError);
			}
		}

		if (project.Name.empty())
			project.Name = std::filesystem::path(rootDir).filename().string();
		if (project.EngineVersion.empty())
			project.EngineVersion = AIM_VERSION;

		ResolvePaths(project);
		return project;
	}

	void CreateDirectoryTree(const AxiomProject& project) {
		Directory::Create(project.RootDirectory);
		Directory::Create(project.AssetsDirectory);
		Directory::Create(project.ScriptsDirectory);
		Directory::Create(project.ScenesDirectory);
		Directory::Create(project.AxiomAssetsDirectory);
		Directory::Create(project.NativeScriptsDir);
		Directory::Create(project.NativeSourceDir);
		Directory::Create(project.PackagesDirectory);
		Directory::Create(Path::Combine(project.RootDirectory, "bin"));
		Directory::Create(Path::Combine(project.RootDirectory, ".vscode"));
	}

	void ConfigurePackages(const AxiomProject& project) {

		const std::filesystem::path engineRoot = ResolveEngineRoot();
		const std::filesystem::path scriptCoreOutput = FindScriptCoreArtifact(engineRoot, "Axiom-ScriptCore.dll");
		const std::filesystem::path scriptCorePdb = FindScriptCoreArtifact(engineRoot, "Axiom-ScriptCore.pdb");

		std::filesystem::path packageDir = std::filesystem::path(project.PackagesDirectory) / "Axiom-ScriptCore";
		std::filesystem::create_directories(packageDir);

		if (std::filesystem::exists(scriptCoreOutput))
			std::filesystem::copy_file(scriptCoreOutput, packageDir / "Axiom-ScriptCore.dll", std::filesystem::copy_options::overwrite_existing);

		if (std::filesystem::exists(scriptCorePdb))
			std::filesystem::copy_file(scriptCorePdb, packageDir / "Axiom-ScriptCore.pdb", std::filesystem::copy_options::overwrite_existing);
	}

	AxiomProject AxiomProject::Create(const std::string& name, const std::string& parentDir,
		const CreateProgressCallback& progressCallback) {
		AxiomProject project;
		project.Name = name;
		project.RootDirectory = Path::Combine(parentDir, name);
		project.EngineVersion = AIM_VERSION;
		ResolvePaths(project);

		ReportCreateProgress(progressCallback, 0.08f, "Creating project folders...");
		CreateDirectoryTree(project);
		ReportCreateProgress(progressCallback, 0.18f, "Configuring engine packages...");
		ConfigurePackages(project);

		std::string engineAssets = Path::ResolveAxiomAssets("");
		if (!engineAssets.empty() && Directory::Exists(engineAssets)) {
			ReportCreateProgress(progressCallback, 0.34f, "Copying Axiom assets...");
			try {
				std::filesystem::copy(engineAssets, project.AxiomAssetsDirectory,
					std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing);
			}
			catch (...) {}
		}

		if (project.BuildSceneList.empty()) {
			project.BuildSceneList.push_back(project.StartupScene);
		}

		// Write axiom-project.json
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
				const bool referencesScriptCore = block.find("Axiom-ScriptCore") != std::string::npos;
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
    <Reference Include="Axiom-ScriptCore">
      <HintPath>Packages\Axiom-ScriptCore\Axiom-ScriptCore.dll</HintPath>
      <Private>true</Private>
    </Reference>
  </ItemGroup>

  <!-- Auto-generated by Axiom Package Manager every time a package is installed/removed. -->
  <Import Project="Packages/AxiomPackages.props" Condition="Exists('Packages/AxiomPackages.props')" />
)CS" + existingPackageRefs + R"CS(
</Project>
)CS";
		File::WriteAllText(project.CsprojPath, csproj);

		// Axiom-ScriptCore is linked via DLL reference in the .csproj — no project entry needed.
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
		std::string starterScript = R"(using Axiom;

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
		AIM_INFO_TAG("AxiomProject", "Created project: {} at {}", name, project.RootDirectory);
		return project;
	}

	std::string AxiomProject::GetEngineRootDir() {
		const std::filesystem::path root = ResolveEngineRoot();
		return root.empty() ? std::string{} : root.generic_string();
	}

	std::string AxiomProject::GetPremakePath() {
		const std::filesystem::path engineRoot = ResolveEngineRoot();
		if (engineRoot.empty()) {
			return {};
		}

#if defined(_WIN32)
		const std::filesystem::path candidate = engineRoot / "vendor" / "bin" / "premake5.exe";
#else
		const std::filesystem::path candidate = engineRoot / "vendor" / "bin" / "premake5";
#endif

		std::error_code ec;
		if (std::filesystem::exists(candidate, ec) && !ec) {
			return candidate.generic_string();
		}
		return {};
	}

	AxiomProject::RegenerateResult AxiomProject::RegenerateSolutionForProject(const std::string& projectRootDir) {
		RegenerateResult result;

		const std::string premakePath = GetPremakePath();
		if (premakePath.empty()) {
			result.Output = "premake5 binary not found under <engine-root>/vendor/bin/. The engine solution cannot be regenerated automatically; the existing one will be used as-is.";
			AIM_WARN_TAG("AxiomProject", "{}", result.Output);
			return result;
		}

		const std::string engineRoot = GetEngineRootDir();
		if (engineRoot.empty()) {
			result.Output = "Could not resolve engine root directory; skipping solution regeneration.";
			AIM_WARN_TAG("AxiomProject", "{}", result.Output);
			return result;
		}

		AIM_INFO_TAG("AxiomProject", "Regenerating engine solution for project: {}", projectRootDir);

		Process::Result processResult = Process::Run({
			premakePath,
			"vs2022",
			"--axiom-project=" + projectRootDir,
		}, engineRoot);

		result.ExitCode = processResult.ExitCode;
		result.Succeeded = processResult.Succeeded();
		result.Output = std::move(processResult.Output);

		if (!result.Succeeded) {
			AIM_ERROR_TAG("AxiomProject", "premake regeneration failed (exit code {}): {}", result.ExitCode, result.Output);
		}
		return result;
	}

	std::string AxiomProject::GetMSBuildPath() {
#if defined(_WIN32)
		const std::vector<std::string> programFilesRoots = {
			"C:\\Program Files\\Microsoft Visual Studio\\2022",
			"C:\\Program Files (x86)\\Microsoft Visual Studio\\2022"
		};
		const std::vector<std::string> editions = { "Community", "Professional", "Enterprise", "BuildTools" };

		for (const auto& root : programFilesRoots) {
			for (const auto& edition : editions) {
				std::filesystem::path candidate(root);
				candidate /= edition;
				candidate /= "MSBuild";
				candidate /= "Current";
				candidate /= "Bin";
				candidate /= "MSBuild.exe";

				std::error_code ec;
				if (std::filesystem::exists(candidate, ec) && !ec) {
					return candidate.generic_string();
				}
			}
		}

		// vswhere fallback for non-default install layouts.
		const std::filesystem::path vswhere = "C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe";
		std::error_code vswhereEc;
		if (std::filesystem::exists(vswhere, vswhereEc) && !vswhereEc) {
			Process::Result vswhereResult = Process::Run({
				vswhere.string(),
				"-latest",
				"-requires", "Microsoft.Component.MSBuild",
				"-find", "MSBuild\\**\\Bin\\MSBuild.exe",
				"-nologo"
			});
			if (vswhereResult.Succeeded()) {
				const std::string& out = vswhereResult.Output;
				const auto newline = out.find_first_of("\r\n");
				std::string firstLine = (newline == std::string::npos) ? out : out.substr(0, newline);
				while (!firstLine.empty() && (firstLine.back() == ' ' || firstLine.back() == '\t')) {
					firstLine.pop_back();
				}
				std::error_code existEc;
				if (!firstLine.empty() && std::filesystem::exists(firstLine, existEc) && !existEc) {
					return firstLine;
				}
			}
		}
#endif
		// Last resort: rely on PATH (returns empty string is also valid; caller can decide).
		return "MSBuild.exe";
	}

	AxiomProject::BuildResult AxiomProject::BuildSolution(const std::string& configuration,
		const std::string& platform) {
		BuildResult result;

		const std::string engineRoot = GetEngineRootDir();
		if (engineRoot.empty()) {
			result.Output = "Could not resolve engine root directory; skipping build.";
			AIM_WARN_TAG("AxiomProject", "{}", result.Output);
			return result;
		}

		const std::filesystem::path solutionPath = std::filesystem::path(engineRoot) / "Axiom.sln";
		std::error_code ec;
		if (!std::filesystem::exists(solutionPath, ec) || ec) {
			result.Output = "Axiom.sln not found at " + solutionPath.generic_string();
			AIM_WARN_TAG("AxiomProject", "{}", result.Output);
			return result;
		}

		const std::string msbuild = GetMSBuildPath();

		AIM_INFO_TAG("AxiomProject", "Building Axiom.sln (Configuration={}, Platform={}) via {}...",
			configuration, platform, msbuild);

		Process::Result processResult = Process::Run({
			msbuild,
			solutionPath.generic_string(),
			"-p:Configuration=" + configuration,
			"-p:Platform=" + platform,
			"-m",
			"-verbosity:minimal",
			"-nologo"
		}, engineRoot);

		result.ExitCode = processResult.ExitCode;
		result.Succeeded = processResult.Succeeded();
		result.Output = std::move(processResult.Output);

		if (!result.Succeeded) {
			AIM_ERROR_TAG("AxiomProject", "MSBuild failed (exit code {}): {}", result.ExitCode, result.Output);
		}
		else {
			AIM_INFO_TAG("AxiomProject", "MSBuild succeeded.");
		}
		return result;
	}

	AxiomProject::BuildResult AxiomProject::BuildSolutionTargets(const std::vector<std::string>& targets,
		const std::string& configuration, const std::string& platform) {
		BuildResult result;

		// Empty target list = success no-op. A project with no local packages skips MSBuild entirely.
		if (targets.empty()) {
			result.Succeeded = true;
			result.ExitCode = 0;
			result.Output = "No targets to build.";
			return result;
		}

		const std::string engineRoot = GetEngineRootDir();
		if (engineRoot.empty()) {
			result.Output = "Could not resolve engine root directory; skipping build.";
			AIM_WARN_TAG("AxiomProject", "{}", result.Output);
			return result;
		}

		const std::filesystem::path solutionPath = std::filesystem::path(engineRoot) / "Axiom.sln";
		std::error_code ec;
		if (!std::filesystem::exists(solutionPath, ec) || ec) {
			result.Output = "Axiom.sln not found at " + solutionPath.generic_string();
			AIM_WARN_TAG("AxiomProject", "{}", result.Output);
			return result;
		}

		// MSBuild expects -t:X;Y;Z with semicolons; solution targets are dot-separated (Pkg.Foo.Native).
		std::string targetsArg = "-t:";
		for (size_t i = 0; i < targets.size(); ++i) {
			if (i > 0) targetsArg += ";";
			targetsArg += targets[i];
		}

		const std::string msbuild = GetMSBuildPath();

		AIM_INFO_TAG("AxiomProject", "Building {} target(s) in Axiom.sln (Configuration={}, Platform={}): {}",
			targets.size(), configuration, platform, targetsArg);

		Process::Result processResult = Process::Run({
			msbuild,
			solutionPath.generic_string(),
			"-p:Configuration=" + configuration,
			"-p:Platform=" + platform,
			targetsArg,
			"-m",
			"-restore", // freshly generated csproj fails with NETSDK1004 without restore
			"-verbosity:minimal",
			"-nologo"
		}, engineRoot);

		result.ExitCode = processResult.ExitCode;
		result.Succeeded = processResult.Succeeded();
		result.Output = std::move(processResult.Output);

		if (!result.Succeeded) {
			AIM_ERROR_TAG("AxiomProject", "MSBuild failed (exit code {}): {}", result.ExitCode, result.Output);
		}
		else {
			AIM_INFO_TAG("AxiomProject", "MSBuild target build succeeded.");
		}
		return result;
	}

	std::vector<std::string> AxiomProject::EnumerateProjectLocalPackages(const std::string& projectRoot) {
		// Returns directory names under <projectRoot>/Packages containing an axiom-package.lua.
		std::vector<std::string> names;
		if (projectRoot.empty()) return names;

		const std::filesystem::path packagesDir = std::filesystem::path(projectRoot) / "Packages";
		std::error_code ec;
		if (!std::filesystem::exists(packagesDir, ec) || ec) return names;
		if (!std::filesystem::is_directory(packagesDir, ec) || ec) return names;

		for (const auto& entry : std::filesystem::directory_iterator(packagesDir, ec)) {
			if (ec) break;
			if (!entry.is_directory(ec) || ec) continue;
			const std::filesystem::path manifest = entry.path() / "axiom-package.lua";
			if (std::filesystem::exists(manifest, ec) && !ec) {
				names.push_back(entry.path().filename().string());
			}
		}

		return names;
	}

	AxiomProject::AutomateResult AxiomProject::AutomateForProject(const std::string& projectRootDir,
		const std::string& configuration, const std::string& platform) {
		AutomateResult result;
		result.Regenerate = RegenerateSolutionForProject(projectRootDir);

		// ExitCode == -1 means premake was missing — the existing solution is still valid, proceed.
		const bool premakeRanAndFailed = !result.Regenerate.Succeeded && result.Regenerate.ExitCode != -1;
		if (premakeRanAndFailed) {
			AIM_ERROR_TAG("AxiomProject", "Skipping build because premake regeneration failed.");
			return result;
		}

		// Build local packages only — relinking Axiom-Engine.dll would fail (loaded by caller).
		std::vector<std::string> targets;
		for (const std::string& packageName : EnumerateProjectLocalPackages(projectRootDir)) {
			// Up to two targets per package; MSBuild silently ignores unresolved ones.
			targets.push_back("Pkg." + packageName + ".Native");
			targets.push_back("Pkg." + packageName);
		}

		result.Build = BuildSolutionTargets(targets, configuration, platform);
		result.RanBuild = !targets.empty();
		return result;
	}

} // namespace Axiom
