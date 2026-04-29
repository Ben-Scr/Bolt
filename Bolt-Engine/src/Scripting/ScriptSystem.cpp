#include "pch.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/NativeScript.hpp"
#include "Scene/Scene.hpp"
#include "Components/Tags.hpp"
#include "Physics/Collision2D.hpp"
#include "Core/Log.hpp"
#include "Core/Application.hpp"
#include "Serialization/Path.hpp"
#include "Project/ProjectManager.hpp"

#include <imgui.h>
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <exception>
#include <filesystem>
#include <optional>

#if defined(BT_PLATFORM_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace Bolt {
	struct ScriptSystemProcessTaskState {
		std::mutex Mutex;
		std::optional<Process::Result> Result;
		std::atomic<bool> Completed{ false };
		std::atomic<bool> Abandoned{ false };
		std::chrono::steady_clock::time_point StartedAt{ std::chrono::steady_clock::now() };
	};

	namespace {
		using ProcessTaskState = ScriptSystemProcessTaskState;

		std::string GetNativeLibraryFilename(const std::string& targetName)
		{
#if defined(BT_PLATFORM_WINDOWS)
			return targetName + ".dll";
#elif defined(BT_PLATFORM_LINUX)
			return "lib" + targetName + ".so";
#else
#error Unsupported platform for native scripts
#endif
		}

		std::filesystem::path NormalizeNativePath(std::filesystem::path path)
		{
			std::error_code ec;
			if (std::filesystem::exists(path)) {
				const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path, ec);
				if (!ec) {
					return canonicalPath;
				}
			}

			return path.lexically_normal();
		}

		std::filesystem::path ResolveProjectNativeDLLPath(const BoltProject& project)
		{
			return NormalizeNativePath(std::filesystem::path(project.GetNativeDllPath()));
		}

		std::filesystem::path ResolveStandaloneNativeDLLPath(const std::filesystem::path& executableDirectory)
		{
			return NormalizeNativePath(
				executableDirectory / ".." / "Bolt-NativeScripts" / GetNativeLibraryFilename("Bolt-NativeScripts"));
		}

		std::string GetActiveNativeBuildConfig()
		{
			return BoltProject::GetActiveBuildConfiguration();
		}

		std::string TrimWhitespace(std::string value)
		{
			const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
				return std::isspace(ch) != 0;
			});
			const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
				return std::isspace(ch) != 0;
			}).base();

			if (first >= last) {
				return {};
			}

			return std::string(first, last);
		}

		std::string ToLowerCopy(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
			return value;
		}

#if defined(BT_PLATFORM_WINDOWS)
		std::optional<std::filesystem::path> NormalizeExecutablePath(std::filesystem::path candidate, const char* executableName)
		{
			if (candidate.empty()) {
				return std::nullopt;
			}

			std::error_code ec;
			if (std::filesystem::is_directory(candidate, ec) && !ec) {
				candidate /= executableName;
			}

			ec.clear();
			if (!std::filesystem::is_regular_file(candidate, ec) || ec) {
				return std::nullopt;
			}

			ec.clear();
			const std::filesystem::path canonical = std::filesystem::weakly_canonical(candidate, ec);
			return ec ? candidate.lexically_normal() : canonical;
		}

		std::optional<std::filesystem::path> GetEnvironmentExecutablePath(const char* variableName, const char* executableName)
		{
			const char* value = std::getenv(variableName);
			if (value == nullptr || value[0] == '\0') {
				return std::nullopt;
			}

			return NormalizeExecutablePath(std::filesystem::path(value), executableName);
		}

		std::optional<std::filesystem::path> SearchExecutableOnPath(const char* executableName)
		{
			char stackBuffer[MAX_PATH]{};
			char* filePart = nullptr;
			DWORD length = SearchPathA(nullptr, executableName, nullptr, MAX_PATH, stackBuffer, &filePart);
			if (length > 0 && length < MAX_PATH) {
				return NormalizeExecutablePath(std::filesystem::path(stackBuffer), executableName);
			}

			if (length >= MAX_PATH) {
				std::string dynamicBuffer(static_cast<size_t>(length) + 1, '\0');
				length = SearchPathA(nullptr, executableName, nullptr,
					static_cast<DWORD>(dynamicBuffer.size()), dynamicBuffer.data(), &filePart);
				if (length > 0 && length < dynamicBuffer.size()) {
					dynamicBuffer.resize(length);
					return NormalizeExecutablePath(std::filesystem::path(dynamicBuffer), executableName);
				}
			}

			return std::nullopt;
		}

		void AppendCMakeCandidate(std::vector<std::filesystem::path>& candidates,
			const char* environmentVariable,
			const std::filesystem::path& relativePath)
		{
			const char* root = std::getenv(environmentVariable);
			if (root == nullptr || root[0] == '\0') {
				return;
			}

			candidates.emplace_back(std::filesystem::path(root) / relativePath);
		}

		std::optional<std::filesystem::path> ResolveVisualStudioCMakeWithVswhere()
		{
			const char* programFilesX86 = std::getenv("ProgramFiles(x86)");
			if (programFilesX86 == nullptr || programFilesX86[0] == '\0') {
				return std::nullopt;
			}

			const std::filesystem::path vswherePath =
				std::filesystem::path(programFilesX86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
			auto normalizedVswhere = NormalizeExecutablePath(vswherePath, "vswhere.exe");
			if (!normalizedVswhere) {
				return std::nullopt;
			}

			Process::Result result = Process::Run({
				normalizedVswhere->string(),
				"-latest",
				"-requires", "Microsoft.VisualStudio.Component.VC.CMake.Project",
				"-property", "installationPath"
			});

			if (!result.Succeeded()) {
				return std::nullopt;
			}

			const std::string installPath = TrimWhitespace(result.Output);
			if (installPath.empty()) {
				return std::nullopt;
			}

			return NormalizeExecutablePath(
				std::filesystem::path(installPath) / "Common7" / "IDE" / "CommonExtensions" /
					"Microsoft" / "CMake" / "CMake" / "bin" / "cmake.exe",
				"cmake.exe");
		}

		std::optional<std::string> ResolveCMakeExecutable()
		{
			static std::optional<std::string> cachedCMakeExecutable;
			if (cachedCMakeExecutable) {
				return cachedCMakeExecutable;
			}

			for (const char* variableName : { "BOLT_CMAKE_PATH", "CMAKE_COMMAND", "CMAKE_EXE" }) {
				if (auto path = GetEnvironmentExecutablePath(variableName, "cmake.exe")) {
					cachedCMakeExecutable = path->string();
					return cachedCMakeExecutable;
				}
			}

			if (auto path = SearchExecutableOnPath("cmake.exe")) {
				cachedCMakeExecutable = path->string();
				return cachedCMakeExecutable;
			}

			if (auto path = ResolveVisualStudioCMakeWithVswhere()) {
				cachedCMakeExecutable = path->string();
				return cachedCMakeExecutable;
			}

			std::vector<std::filesystem::path> candidates;
			AppendCMakeCandidate(candidates, "ProgramFiles", std::filesystem::path("CMake") / "bin" / "cmake.exe");
			AppendCMakeCandidate(candidates, "ProgramFiles(x86)", std::filesystem::path("CMake") / "bin" / "cmake.exe");

			for (const char* version : { "2022", "2019" }) {
				for (const char* edition : { "Enterprise", "Professional", "Community", "BuildTools" }) {
					AppendCMakeCandidate(candidates, "ProgramFiles",
						std::filesystem::path("Microsoft Visual Studio") / version / edition / "Common7" /
							"IDE" / "CommonExtensions" / "Microsoft" / "CMake" / "CMake" / "bin" / "cmake.exe");
					AppendCMakeCandidate(candidates, "ProgramFiles(x86)",
						std::filesystem::path("Microsoft Visual Studio") / version / edition / "Common7" /
							"IDE" / "CommonExtensions" / "Microsoft" / "CMake" / "CMake" / "bin" / "cmake.exe");
				}
			}

			for (const std::filesystem::path& candidate : candidates) {
				if (auto path = NormalizeExecutablePath(candidate, "cmake.exe")) {
					cachedCMakeExecutable = path->string();
					return cachedCMakeExecutable;
				}
			}

			return std::nullopt;
		}
#else
		std::optional<std::string> ResolveCMakeExecutable()
		{
			return std::string("cmake");
		}
#endif

		std::vector<std::string> GetManagedWatchPatterns()
		{
			return {
				".cs",
				".csproj",
				".props",
				".targets",
				".sln"
			};
		}

		std::vector<std::string> BuildManagedWatchTargets(
			const std::filesystem::path& scriptsDirectory,
			const std::filesystem::path& projectFile,
			const std::filesystem::path& solutionFile = {})
		{
			std::vector<std::string> targets;
			auto appendTarget = [&targets](const std::filesystem::path& path) {
				if (!path.empty()) {
					targets.push_back(path.string());
				}
			};

			appendTarget(scriptsDirectory);
			appendTarget(projectFile);
			appendTarget(solutionFile);

			const std::filesystem::path projectRoot = projectFile.parent_path();
			if (!projectRoot.empty()) {
				appendTarget(projectRoot / "Directory.Build.props");
				appendTarget(projectRoot / "Directory.Build.targets");
			}

			return targets;
		}

		std::vector<std::string> GetNativeWatchPatterns()
		{
			return {
				".c",
				".cc",
				".cpp",
				".cxx",
				".h",
				".hh",
				".hpp",
				".hxx",
				".inl",
				".ipp"
			};
		}

		bool IsNativeScriptBootstrapFile(const std::filesystem::path& path)
		{
			return path.filename().string() == "NativeScriptExports.cpp";
		}

		bool IsNativeScriptSourceExtension(const std::filesystem::path& path)
		{
			std::string extension = path.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});

			return extension == ".c" || extension == ".cc" || extension == ".cpp" || extension == ".cxx";
		}

		bool HasNativeScriptSourceFiles(const std::filesystem::path& sourceDirectory)
		{
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

		std::vector<std::string> BuildNativeWatchTargets(
			const std::filesystem::path& projectDirectory,
			const std::filesystem::path& sourceDirectory = {})
		{
			std::vector<std::string> targets;
			auto appendTarget = [&targets](const std::filesystem::path& path) {
				if (!path.empty()) {
					targets.push_back(path.string());
				}
			};

			appendTarget(sourceDirectory);
			appendTarget(projectDirectory / "Include");

			if (targets.empty()) {
				appendTarget(projectDirectory);
			}

			return targets;
		}

		bool ShouldIgnoreBuildInputDirectory(const std::filesystem::path& directoryPath)
		{
			const std::string name = ToLowerCopy(directoryPath.filename().string());
			return name == ".git"
				|| name == ".vs"
				|| name == ".idea"
				|| name == "bin"
				|| name == "bin-int"
				|| name == "obj"
				|| name == "build"
				|| name == "out";
		}

		bool MatchesBuildInputPattern(const std::filesystem::path& filePath, const std::vector<std::string>& patterns)
		{
			if (patterns.empty()) {
				return true;
			}

			const std::string fileName = ToLowerCopy(filePath.filename().string());
			const std::string extension = ToLowerCopy(filePath.extension().string());

			for (const std::string& pattern : patterns) {
				const std::string normalized = ToLowerCopy(pattern);
				if (normalized.empty()) {
					continue;
				}

				if (normalized.front() == '.') {
					if (extension == normalized) {
						return true;
					}
				}
				else if (fileName == normalized) {
					return true;
				}
			}

			return false;
		}

		void UpdateLatestBuildInputTime(
			const std::filesystem::path& filePath,
			std::filesystem::file_time_type& latestWriteTime,
			bool& foundInput)
		{
			std::error_code ec;
			const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(filePath, ec);
			if (ec) {
				return;
			}

			if (!foundInput || writeTime > latestWriteTime) {
				latestWriteTime = writeTime;
				foundInput = true;
			}
		}

		bool TryGetLatestBuildInputTime(
			const std::vector<std::string>& targets,
			const std::vector<std::string>& patterns,
			std::filesystem::file_time_type& latestWriteTime)
		{
			bool foundInput = false;

			for (const std::string& targetString : targets) {
				if (targetString.empty()) {
					continue;
				}

				const std::filesystem::path target(targetString);
				std::error_code ec;
				if (std::filesystem::is_directory(target, ec) && !ec) {
					for (std::filesystem::recursive_directory_iterator it(target, std::filesystem::directory_options::skip_permission_denied, ec), end;
						 it != end;
						 it.increment(ec)) {
						if (ec) {
							ec.clear();
							continue;
						}

						if (it->is_directory(ec)) {
							if (!ec && ShouldIgnoreBuildInputDirectory(it->path())) {
								it.disable_recursion_pending();
							}
							ec.clear();
							continue;
						}

						if (!it->is_regular_file(ec) || ec || !MatchesBuildInputPattern(it->path(), patterns)) {
							ec.clear();
							continue;
						}

						UpdateLatestBuildInputTime(it->path(), latestWriteTime, foundInput);
					}

					continue;
				}

				ec.clear();
				if (!std::filesystem::is_regular_file(target, ec) || ec || !MatchesBuildInputPattern(target, patterns)) {
					continue;
				}

				UpdateLatestBuildInputTime(target, latestWriteTime, foundInput);
			}

			return foundInput;
		}

		bool BuildArtifactIsStale(
			const std::vector<std::string>& inputTargets,
			const std::vector<std::string>& inputPatterns,
			const std::filesystem::path& artifactPath)
		{
			std::filesystem::file_time_type latestInputWriteTime{};
			if (!TryGetLatestBuildInputTime(inputTargets, inputPatterns, latestInputWriteTime)) {
				return false;
			}

			std::error_code ec;
			if (artifactPath.empty() || !std::filesystem::exists(artifactPath, ec) || ec) {
				return true;
			}

			const std::filesystem::file_time_type artifactWriteTime = std::filesystem::last_write_time(artifactPath, ec);
			if (ec) {
				return true;
			}

			return latestInputWriteTime > artifactWriteTime;
		}

		template <typename TFunc>
		void ForEachLoadedScene(TFunc&& func)
		{
			Application* app = Application::GetInstance();
			if (!app || !app->GetSceneManager()) {
				return;
			}

			app->GetSceneManager()->ForeachLoadedScene(std::forward<TFunc>(func));
		}

		void RemovePendingFieldValuesForClass(ScriptComponent& scriptComp, const std::string& className)
		{
			if (className.empty()) {
				return;
			}

			for (const ScriptInstance& instance : scriptComp.Scripts) {
				if (instance.GetClassName() == className && instance.GetType() != ScriptType::Native) {
					return;
				}
			}

			const std::string prefix = className + ".";
			for (auto it = scriptComp.PendingFieldValues.begin(); it != scriptComp.PendingFieldValues.end(); ) {
				if (it->first.rfind(prefix, 0) == 0) {
					it = scriptComp.PendingFieldValues.erase(it);
				}
				else {
					++it;
				}
			}
		}

		std::vector<std::string> GetActiveGlobalSystemClassNames()
		{
			std::vector<std::string> classNames;
			BoltProject* project = ProjectManager::GetCurrentProject();
			if (!project) {
				return classNames;
			}

			for (const auto& registration : project->GlobalSystems) {
				if (registration.Active && !registration.ClassName.empty()) {
					classNames.push_back(registration.ClassName);
				}
			}
			return classNames;
		}

		void InitializeRegisteredGlobalSystems()
		{
			ScriptEngine::InitializeGlobalSystems(GetActiveGlobalSystemClassNames());
		}

		struct ScriptSceneScope {
			explicit ScriptSceneScope(Scene& scene)
				: Previous(ScriptEngine::GetScene())
			{
				ScriptEngine::SetScene(&scene);
			}

			~ScriptSceneScope()
			{
				ScriptEngine::SetScene(Previous);
			}

			Scene* Previous = nullptr;
		};

		uint64_t ToManagedEntityID(Scene& scene, EntityHandle entity)
		{
			uint64_t entityID = static_cast<uint64_t>(static_cast<uint32_t>(entity));
			if (scene.IsValid(entity)) {
				const uint64_t runtimeID = scene.GetRuntimeID(entity);
				if (runtimeID != 0) {
					entityID = runtimeID;
				}
			}
			return entityID;
		}

		void TeardownManagedScriptInstance(Scene& scene, ScriptInstance& instance)
		{
			if (!instance.HasManagedInstance() || !ScriptEngine::IsInitialized()) {
				return;
			}

			ScriptSceneScope sceneScope(scene);
			if (instance.HasEnabled()) {
				ScriptEngine::InvokeOnDisable(instance.GetGCHandle());
				instance.MarkDisabled();
			}
			ScriptEngine::InvokeOnDestroy(instance.GetGCHandle());
			ScriptEngine::DestroyScriptInstance(instance.GetGCHandle());
			instance.Unbind();
		}

		void TeardownNativeScriptInstance(Scene& scene, ScriptInstance& instance, NativeScriptHost& nativeHost)
		{
			if (!instance.HasNativeInstance()) {
				return;
			}

			ScriptSceneScope sceneScope(scene);
			if (nativeHost.IsLoaded()) {
				nativeHost.DestroyInstance(instance.GetNativePtr());
			}
			instance.Unbind();
		}

		template<typename Fn>
		bool TryInvokeNativeScript(const ScriptInstance& instance, const char* callbackName, Fn&& callback)
		{
			try {
				callback();
				return true;
			}
			catch (const std::exception& e) {
				BT_CORE_ERROR_TAG("ScriptSystem", "Native script '{}' failed during {}: {}",
					instance.GetClassName(), callbackName, e.what());
			}
			catch (...) {
				BT_CORE_ERROR_TAG("ScriptSystem", "Native script '{}' failed during {} with an unknown exception",
					instance.GetClassName(), callbackName);
			}

			return false;
		}

		bool IsTaskRunning(const std::shared_ptr<ProcessTaskState>& task)
		{
			return task && !task->Completed.load(std::memory_order_acquire);
		}

		std::shared_ptr<ProcessTaskState> LaunchTask(std::function<Process::Result()> work)
		{
			auto task = std::make_shared<ProcessTaskState>();
			task->StartedAt = std::chrono::steady_clock::now();

			std::thread([task, work = std::move(work)]() mutable {
				Process::Result result{};
				try {
					result = work();
				}
				catch (const std::exception& e) {
					result.ExitCode = -1;
					result.Output = e.what();
				}
				catch (...) {
					result.ExitCode = -1;
					result.Output = "Unknown exception while running background rebuild task.";
				}

				{
					std::scoped_lock lock(task->Mutex);
					task->Result = std::move(result);
				}

				task->Completed.store(true, std::memory_order_release);
			}).detach();

			return task;
		}

		bool TryTakeTaskResult(std::shared_ptr<ProcessTaskState>& task, Process::Result& outResult)
		{
			if (!task || !task->Completed.load(std::memory_order_acquire)) {
				return false;
			}

			if (task->Abandoned.load(std::memory_order_acquire)) {
				task.reset();
				return false;
			}

			{
				std::scoped_lock lock(task->Mutex);
				outResult = task->Result.value_or(Process::Result{});
			}

			task.reset();
			return true;
		}

		bool NativeBuildNeedsConfigure(const std::filesystem::path& nativeProjectDirectory, const std::filesystem::path& buildDirectory)
		{
			std::error_code ec;
			const std::filesystem::path cachePath = buildDirectory / "CMakeCache.txt";
			if (!std::filesystem::exists(cachePath, ec) || ec) {
				return true;
			}

			const std::filesystem::path cmakeListsPath = nativeProjectDirectory / "CMakeLists.txt";
			if (!std::filesystem::exists(cmakeListsPath, ec) || ec) {
				return false;
			}

			const auto cacheWriteTime = std::filesystem::last_write_time(cachePath, ec);
			if (ec) {
				return true;
			}

			const auto cmakeWriteTime = std::filesystem::last_write_time(cmakeListsPath, ec);
			if (ec) {
				return false;
			}

			return cmakeWriteTime > cacheWriteTime;
		}

		bool NativeBuildCacheExists(const std::filesystem::path& buildDirectory)
		{
			std::error_code ec;
			return std::filesystem::exists(buildDirectory / "CMakeCache.txt", ec) && !ec;
		}

		void AbandonTask(std::shared_ptr<ProcessTaskState>& task, std::string_view description)
		{
			if (!task) {
				return;
			}

			if (!task->Completed.load(std::memory_order_acquire)) {
				task->Abandoned.store(true, std::memory_order_release);
				BT_CORE_WARN_TAG("ScriptSystem", "{} is still running during teardown; the result will be ignored so shutdown stays non-blocking", description);
			}

			task.reset();
		}
	}

	void ScriptSystem::Awake(Scene& scene)
	{
		m_LastScene = &scene;
		++m_ActiveSystemCount;
		if (m_PollingOwner == nullptr) {
			m_PollingOwner = this;
		}
		if (m_ActiveSystemCount > 1) {
			return;
		}

		auto exeDir = std::filesystem::path(Path::ExecutableDir());
		BoltProject* project = ProjectManager::GetCurrentProject();

		if (!ScriptEngine::IsInitialized()) {
			ScriptEngine::Init();
		}

		// Core assembly — check multiple locations
		if (m_CoreAssemblyPath.empty())
		{
			std::vector<std::filesystem::path> candidates = {
				exeDir / ".." / "Bolt-ScriptCore" / "Bolt-ScriptCore.dll",  // dev layout
				exeDir / "Bolt-ScriptCore.dll",                              // packaged build (beside exe)
			};
			for (const auto& candidate : candidates) {
				if (std::filesystem::exists(candidate)) {
					m_CoreAssemblyPath = std::filesystem::canonical(candidate).string();
					break;
				}
			}
		}

		if (!ScriptEngine::IsInitialized() && std::filesystem::exists(m_CoreAssemblyPath))
			ScriptEngine::LoadCoreAssembly(m_CoreAssemblyPath);

		// User assembly — project-aware
		if (m_UserAssemblyPath.empty())
		{
			if (project)
			{
				auto userDll = std::filesystem::path(project->GetUserAssemblyOutputPath());
				m_UserAssemblyPath = userDll.string();
				if (std::filesystem::exists(userDll))
					m_UserAssemblyPath = std::filesystem::canonical(userDll).string();
			}
			else
			{
				auto sandboxPath = exeDir / ".." / "Bolt-Sandbox" / "Bolt-Sandbox.dll";
				m_UserAssemblyPath = sandboxPath.string();
				if (std::filesystem::exists(sandboxPath))
					m_UserAssemblyPath = std::filesystem::canonical(sandboxPath).string();
			}
		}

		if (!ScriptEngine::HasUserAssembly() && !m_UserAssemblyPath.empty() && std::filesystem::exists(m_UserAssemblyPath)) {
			ScriptEngine::LoadUserAssembly(m_UserAssemblyPath);
		}
		InitializeRegisteredGlobalSystems();

		// C# file watcher — project-aware
		if (project)
		{
			auto scriptsDir = std::filesystem::path(project->ScriptsDirectory);
			auto csproj = std::filesystem::path(project->CsprojPath);
			if (std::filesystem::exists(csproj))
			{
				std::vector<std::string> managedWatchTargets =
					BuildManagedWatchTargets(scriptsDir, csproj, std::filesystem::path(project->SlnPath));
				m_SandboxProjectPath = std::filesystem::canonical(csproj).string();
				m_ScriptWatcher.Watch(
					managedWatchTargets,
					GetManagedWatchPatterns(),
					[this]() { RebuildAndReloadScripts(); });

				if (BuildArtifactIsStale(managedWatchTargets, GetManagedWatchPatterns(), std::filesystem::path(m_UserAssemblyPath))) {
					RebuildAndReloadScripts();
				}
			}
		}
		else
		{
			auto sandboxProjectDir = exeDir / ".." / ".." / ".." / "Bolt-Sandbox";
			auto sandboxSourceDir = sandboxProjectDir / "Source";
			auto sandboxCsproj = sandboxProjectDir / "Bolt-Sandbox.csproj";
			if (std::filesystem::exists(sandboxCsproj))
			{
				std::vector<std::string> managedWatchTargets =
					BuildManagedWatchTargets(sandboxSourceDir, sandboxCsproj);
				m_SandboxProjectPath = std::filesystem::canonical(sandboxCsproj).string();
				m_ScriptWatcher.Watch(
					managedWatchTargets,
					GetManagedWatchPatterns(),
					[this]() { RebuildAndReloadScripts(); });

				if (BuildArtifactIsStale(managedWatchTargets, GetManagedWatchPatterns(), std::filesystem::path(m_UserAssemblyPath))) {
					RebuildAndReloadScripts();
				}
			}
		}

		// Native C++ scripting — project-aware
		if (project)
		{
			const bool hasNativeScriptSources = project->HasNativeScriptSources();
			if (hasNativeScriptSources) {
				project->EnsureNativeScriptProjectFiles();
			}

			m_NativeProjectDirectory = NormalizeNativePath(project->NativeScriptsDir).string();
			m_NativeSourceDirectory = NormalizeNativePath(project->NativeSourceDir).string();
			const std::filesystem::path nativeDll = ResolveProjectNativeDLLPath(*project);
			m_NativeDLLPath = nativeDll.string();
			m_NativeTargetName = project->Name + "-NativeScripts";
			if (hasNativeScriptSources && std::filesystem::exists(nativeDll))
			{
				m_NativeHost.LoadDLL(m_NativeDLLPath);
			}
			if (!m_NativeProjectDirectory.empty())
			{
				std::vector<std::string> nativeWatchTargets =
					BuildNativeWatchTargets(std::filesystem::path(m_NativeProjectDirectory), std::filesystem::path(m_NativeSourceDirectory));
				m_NativeWatcher.Watch(
					nativeWatchTargets,
					GetNativeWatchPatterns(),
					[this]() { RebuildAndReloadNativeScripts(); });

				if (hasNativeScriptSources && BuildArtifactIsStale(nativeWatchTargets, GetNativeWatchPatterns(), std::filesystem::path(m_NativeDLLPath))) {
					RebuildAndReloadNativeScripts();
				}
			}
		}
		else
		{
			auto nativeProjectDir = exeDir / ".." / ".." / ".." / "Bolt-NativeScripts";
			auto nativeSourceDir = nativeProjectDir / "Source";
			const bool hasNativeScriptSources = HasNativeScriptSourceFiles(nativeSourceDir);
			m_NativeProjectDirectory = NormalizeNativePath(nativeProjectDir).string();
			m_NativeSourceDirectory = NormalizeNativePath(nativeSourceDir).string();

			const std::filesystem::path nativeDll = ResolveStandaloneNativeDLLPath(exeDir);
			m_NativeDLLPath = nativeDll.string();
			m_NativeTargetName = "Bolt-NativeScripts";
			if (hasNativeScriptSources && std::filesystem::exists(nativeDll))
			{
				m_NativeHost.LoadDLL(m_NativeDLLPath);
			}
			if (!m_NativeProjectDirectory.empty())
			{
				std::vector<std::string> nativeWatchTargets =
					BuildNativeWatchTargets(std::filesystem::path(m_NativeProjectDirectory), std::filesystem::path(m_NativeSourceDirectory));
				m_NativeWatcher.Watch(
					nativeWatchTargets,
					GetNativeWatchPatterns(),
					[this]() { RebuildAndReloadNativeScripts(); });

				if (hasNativeScriptSources && BuildArtifactIsStale(nativeWatchTargets, GetNativeWatchPatterns(), std::filesystem::path(m_NativeDLLPath))) {
					RebuildAndReloadNativeScripts();
				}
			}
		}

		BT_INFO_TAG("ScriptSystem", "Script system initialized");
	}

	// ── C# rebuild ─────────────────────────────────────────────────────

	void ScriptSystem::RebuildAndReloadScripts()
	{
		if (m_RebuildTask) {
			m_RebuildQueued = true;
			return;
		}
		if (m_SandboxProjectPath.empty()) {
			m_RebuildQueued = false;
			return;
		}

		BT_INFO_TAG("ScriptSystem", "Rebuilding C# scripts...");
		m_LastRebuildFailed = false;
		const std::string sandboxProjectPath = m_SandboxProjectPath;
		m_RebuildTask = LaunchTask([sandboxProjectPath]() {
			return Process::Run({
				"dotnet",
				"build",
				sandboxProjectPath,
				"-c", BoltProject::GetActiveBuildConfiguration(),
				"--nologo",
				"-v", "q",
				"-p:DefineConstants=" + BoltProject::BuildManagedDefineConstants("BOLT_EDITOR")
			});
		});
	}

	// ── C++ native rebuild ─────────────────────────────────────────────

	void ScriptSystem::RebuildAndReloadNativeScripts()
	{
		if (m_NativeRebuildTask) {
			m_NativeRebuildQueued = true;
			return;
		}
		if (m_NativeProjectDirectory.empty()) {
			m_NativeRebuildQueued = false;
			return;
		}

		BoltProject* project = ProjectManager::GetCurrentProject();
		const bool hasNativeScriptSources = project
			? project->HasNativeScriptSources()
			: HasNativeScriptSourceFiles(std::filesystem::path(m_NativeSourceDirectory));

		if (!hasNativeScriptSources) {
			m_LastRebuildFailed = false;
			if (m_NativeHost.IsLoaded()) {
				ForEachLoadedScene([this](Scene& loadedScene) { TeardownNativeScripts(loadedScene); });
				m_NativeHost.UnloadDLL();
				BT_INFO_TAG("ScriptSystem", "Native script sources removed; unloaded native script DLL");
			}
			return;
		}

		if (project) {
			project->EnsureNativeScriptProjectFiles();
			m_NativeProjectDirectory = NormalizeNativePath(project->NativeScriptsDir).string();
			m_NativeSourceDirectory = NormalizeNativePath(project->NativeSourceDir).string();
			m_NativeTargetName = project->Name + "-NativeScripts";
		}

		BT_INFO_TAG("ScriptSystem", "Rebuilding native scripts...");
		m_LastRebuildFailed = false;
		const std::string nativeProjectDirectory = m_NativeProjectDirectory;
		const std::string buildConfig = GetActiveNativeBuildConfig();
		const std::string nativeTargetName = m_NativeTargetName;
		m_NativeRebuildTask = LaunchTask([nativeProjectDirectory, buildConfig, nativeTargetName]() {
			const std::filesystem::path buildDirectory = std::filesystem::path(nativeProjectDirectory) / "build";
			const std::optional<std::string> cmakeExecutable = ResolveCMakeExecutable();
			if (!cmakeExecutable) {
				Process::Result result{};
				result.Output =
					"CMake executable was not found. Install CMake, install the Visual Studio C++ CMake tools, "
					"add cmake.exe to PATH, or set BOLT_CMAKE_PATH to the cmake.exe location.";
				return result;
			}

			Process::Result configureResult{};
			if (NativeBuildNeedsConfigure(nativeProjectDirectory, buildDirectory)) {
				std::vector<std::string> configureCommand = {
					*cmakeExecutable,
					"-B", buildDirectory.string(),
					"-S", nativeProjectDirectory,
					"-DCMAKE_BUILD_TYPE=" + buildConfig
				};

#if defined(BT_PLATFORM_WINDOWS)
				if (!NativeBuildCacheExists(buildDirectory)) {
					configureCommand.push_back("-G");
					configureCommand.push_back("Visual Studio 17 2022");
					configureCommand.push_back("-A");
					configureCommand.push_back("x64");
				}
#endif

				configureResult = Process::Run(configureCommand, nativeProjectDirectory);
				if (!configureResult.Succeeded()) {
					return configureResult;
				}
			}

			std::vector<std::string> buildCommand = {
				*cmakeExecutable,
				"--build", buildDirectory.string(),
				"--config", buildConfig
			};
			if (!nativeTargetName.empty()) {
				buildCommand.push_back("--target");
				buildCommand.push_back(nativeTargetName);
			}

			Process::Result buildResult = Process::Run(buildCommand, nativeProjectDirectory);
			if (!configureResult.Output.empty()) {
				buildResult.Output = configureResult.Output + buildResult.Output;
			}
			return buildResult;
		});
	}

	bool ScriptSystem::RequestRebuildAndReloadAll()
	{
		const bool wasRebuilding = IsRebuilding();
		m_LastRebuildFailed = false;

		RebuildAndReloadScripts();
		RebuildAndReloadNativeScripts();

		return IsRebuilding() || wasRebuilding;
	}

	bool ScriptSystem::IsRebuilding() const
	{
		return m_RebuildTask != nullptr || m_NativeRebuildTask != nullptr;
	}

	// ── OnGui: hot-reload polling + build overlay ──────────────────────

	void ScriptSystem::TeardownManagedScripts(Scene& scene)
	{
		if (!ScriptEngine::IsInitialized()) {
			return;
		}

		ScriptSceneScope sceneScope(scene);

		auto view = scene.GetRegistry().view<ScriptComponent>();
		for (auto [entity, scriptComp] : view.each())
		{
			for (auto& instance : scriptComp.Scripts)
			{
				TeardownManagedScriptInstance(scene, instance);
			}
		}
	}

	void ScriptSystem::TeardownNativeScripts(Scene& scene)
	{
		ScriptSceneScope sceneScope(scene);

		auto view = scene.GetRegistry().view<ScriptComponent>();
		for (auto [entity, scriptComp] : view.each())
		{
			for (auto& instance : scriptComp.Scripts)
			{
				if (!instance.HasNativeInstance()) {
					continue;
				}

				TeardownNativeScriptInstance(scene, instance, m_NativeHost);
			}
		}
	}

	bool ScriptSystem::RemoveScript(Entity entity, size_t index)
	{
		if (entity.GetScene() == nullptr) {
			return false;
		}

		if (!entity.HasComponent<ScriptComponent>()) {
			return false;
		}

		auto& scriptComp = entity.GetComponent<ScriptComponent>();
		if (index >= scriptComp.Scripts.size()) {
			return false;
		}

		ScriptInstance& instance = scriptComp.Scripts[index];
		const std::string removedClassName = instance.GetClassName();

		if (Scene* scene = entity.GetScene()) {
			TeardownManagedScriptInstance(*scene, instance);
			TeardownNativeScriptInstance(*scene, instance, m_NativeHost);
		}
		else {
			instance.Unbind();
		}

		scriptComp.Scripts.erase(scriptComp.Scripts.begin() + static_cast<ptrdiff_t>(index));
		RemovePendingFieldValuesForClass(scriptComp, removedClassName);
		return true;
	}

	void ScriptSystem::RemoveAllScripts(Entity entity)
	{
		if (entity.GetScene() == nullptr || !entity.HasComponent<ScriptComponent>()) {
			return;
		}

		auto& scriptComp = entity.GetComponent<ScriptComponent>();
		for (size_t index = scriptComp.Scripts.size(); index > 0; --index) {
			RemoveScript(entity, index - 1);
		}

		scriptComp.PendingFieldValues.clear();
	}

	void ScriptSystem::SetScriptsEnabled(Entity entity, bool enabled)
	{
		Scene* scene = entity.GetScene();
		if (scene == nullptr || !entity.HasComponent<ScriptComponent>() || !ScriptEngine::IsInitialized()) {
			return;
		}

		ScriptSceneScope sceneScope(*scene);
		auto& scriptComp = entity.GetComponent<ScriptComponent>();
		for (auto& instance : scriptComp.Scripts)
		{
			if (!instance.HasManagedInstance()) {
				continue;
			}

			if (enabled && !instance.HasEnabled()) {
				ScriptEngine::InvokeOnEnable(instance.GetGCHandle());
				instance.MarkEnabled();
			}
			else if (!enabled && instance.HasEnabled()) {
				ScriptEngine::InvokeOnDisable(instance.GetGCHandle());
				instance.MarkDisabled();
			}
		}
	}

	namespace {
		enum class CollisionDispatchPhase {
			Enter,
			Stay,
			Exit
		};

		void DispatchCollision2D(Scene& scene, const Collision2D& collision, CollisionDispatchPhase phase)
		{
			if (!ScriptEngine::IsInitialized()) {
				return;
			}

			const auto dispatchToEntity = [&](EntityHandle self, EntityHandle other) {
				if (!scene.IsValid(self) || !scene.HasComponent<ScriptComponent>(self) || scene.HasComponent<DisabledTag>(self)) {
					return;
				}

				ScriptSceneScope sceneScope(scene);
				const uint64_t selfID = ToManagedEntityID(scene, self);
				const uint64_t otherID = scene.IsValid(other) ? ToManagedEntityID(scene, other) : 0;
				const uint64_t entityAID = scene.IsValid(collision.entityA) ? ToManagedEntityID(scene, collision.entityA) : 0;
				const uint64_t entityBID = scene.IsValid(collision.entityB) ? ToManagedEntityID(scene, collision.entityB) : 0;

				auto& scriptComp = scene.GetComponent<ScriptComponent>(self);
				for (auto& instance : scriptComp.Scripts)
				{
					if (!instance.HasManagedInstance() || !instance.HasEnabled()) {
						continue;
					}

					switch (phase) {
					case CollisionDispatchPhase::Enter:
						ScriptEngine::InvokeCollisionEnter2D(instance.GetGCHandle(), selfID, otherID, entityAID, entityBID, collision.contactPoint.x, collision.contactPoint.y);
						break;
					case CollisionDispatchPhase::Stay:
						ScriptEngine::InvokeCollisionStay2D(instance.GetGCHandle(), selfID, otherID, entityAID, entityBID, collision.contactPoint.x, collision.contactPoint.y);
						break;
					case CollisionDispatchPhase::Exit:
						ScriptEngine::InvokeCollisionExit2D(instance.GetGCHandle(), selfID, otherID, entityAID, entityBID, collision.contactPoint.x, collision.contactPoint.y);
						break;
					}
				}
			};

			dispatchToEntity(collision.entityA, collision.entityB);
			if (collision.entityB != collision.entityA) {
				dispatchToEntity(collision.entityB, collision.entityA);
			}
		}
	}

	void ScriptSystem::DispatchCollisionEnter2D(Scene& scene, const Collision2D& collision)
	{
		DispatchCollision2D(scene, collision, CollisionDispatchPhase::Enter);
	}

	void ScriptSystem::DispatchCollisionStay2D(Scene& scene, const Collision2D& collision)
	{
		DispatchCollision2D(scene, collision, CollisionDispatchPhase::Stay);
	}

	void ScriptSystem::DispatchCollisionExit2D(Scene& scene, const Collision2D& collision)
	{
		DispatchCollision2D(scene, collision, CollisionDispatchPhase::Exit);
	}

	void ScriptSystem::OnGui(Scene& scene)
	{
		(void)scene;
		if (m_PollingOwner == nullptr) {
			m_PollingOwner = this;
		}
		if (m_PollingOwner != this) {
			return;
		}

		// Skip file watcher polling while a script is being created/renamed
		if (!m_SuppressRecompile) {
			m_ScriptWatcher.Poll(1.0f);
			m_NativeWatcher.Poll(1.0f);
		}

		bool anyRebuilding = false;

		// C# build completion check
		Process::Result result;
		if (TryTakeTaskResult(m_RebuildTask, result))
		{
			if (result.Succeeded())
			{
				ForEachLoadedScene([this](Scene& loadedScene) { TeardownManagedScripts(loadedScene); });
				if (!m_UserAssemblyPath.empty() && std::filesystem::exists(m_UserAssemblyPath)) {
					ScriptEngine::LoadUserAssembly(m_UserAssemblyPath);
				}
				else {
					ScriptEngine::ReloadAssemblies();
				}
				InitializeRegisteredGlobalSystems();
				BT_INFO_TAG("ScriptSystem", "C# scripts rebuilt and reloaded");
			}
			else
			{
				m_LastRebuildFailed = true;
				BT_ERROR_TAG("ScriptSystem", "C# build failed (exit code {})", result.ExitCode);
				if (!result.Output.empty()) {
					BT_ERROR_TAG("ScriptSystem", "{}", result.Output);
				}
			}

			if (m_RebuildQueued) {
				m_RebuildQueued = false;
				RebuildAndReloadScripts();
			}
		}

		// C++ build completion check
		if (TryTakeTaskResult(m_NativeRebuildTask, result))
		{
			if (result.Succeeded())
			{
				const std::filesystem::path exeDir = std::filesystem::path(Path::ExecutableDir());
				if (BoltProject* project = ProjectManager::GetCurrentProject()) {
					m_NativeDLLPath = ResolveProjectNativeDLLPath(*project).string();
				}
				else {
					m_NativeDLLPath = ResolveStandaloneNativeDLLPath(exeDir).string();
				}

				if (!std::filesystem::exists(m_NativeDLLPath)) {
					m_LastRebuildFailed = true;
					BT_ERROR_TAG("ScriptSystem", "Native scripts built, but DLL was not found at '{}'", m_NativeDLLPath);
				}
				else {
					// Swap only after the replacement DLL exists so failed rebuilds keep the previous host alive.
					ForEachLoadedScene([this](Scene& loadedScene) { TeardownNativeScripts(loadedScene); });

					if (!m_NativeHost.LoadDLL(m_NativeDLLPath)) {
						m_LastRebuildFailed = true;
						BT_ERROR_TAG("ScriptSystem", "Native scripts built, but failed to load '{}'", m_NativeDLLPath);
						BT_WARN_TAG("ScriptSystem", "Keeping the previous native script DLL loaded; instances will be recreated on the next update.");
					}
					else {
						BT_INFO_TAG("ScriptSystem", "Native scripts rebuilt and reloaded");
					}
				}
			}
			else
			{
				m_LastRebuildFailed = true;
				BT_ERROR_TAG("ScriptSystem", "Native build failed (exit code {})", result.ExitCode);
				if (!result.Output.empty()) {
					BT_ERROR_TAG("ScriptSystem", "{}", result.Output);
				}
			}

			if (m_NativeRebuildQueued) {
				m_NativeRebuildQueued = false;
				RebuildAndReloadNativeScripts();
			}
		}

		anyRebuilding = IsTaskRunning(m_RebuildTask) || IsTaskRunning(m_NativeRebuildTask);

		// Build overlay
		if (anyRebuilding)
		{
			ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImVec2 center(viewport->Pos.x + viewport->Size.x * 0.5f,
			              viewport->Pos.y + viewport->Size.y * 0.5f);

			ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2(320, 80));
			ImGui::SetNextWindowBgAlpha(0.92f);

			ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
				| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
				| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav;

			ImGui::Begin("##ScriptBuildOverlay", nullptr, flags);
			const bool nativeRebuildRunning = IsTaskRunning(m_NativeRebuildTask);
			ImGui::TextUnformatted(nativeRebuildRunning ? "Compiling Native Scripts..." : "Compiling Scripts...");
			ImGui::Spacing();

			const auto startTime = nativeRebuildRunning ? m_NativeRebuildTask->StartedAt : m_RebuildTask->StartedAt;
			float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - startTime).count();
			ImGui::ProgressBar(fmodf(elapsed * 0.4f, 1.0f), ImVec2(-1, 0), "");

			ImGui::End();
		}
	}

	// ── Update: dual dispatch (Managed + Native) ───────────────────────

	void ScriptSystem::Update(Scene& scene)
	{
		m_LastScene = &scene;
		ScriptSceneScope sceneScope(scene);
		const bool managedRuntimeReady = ScriptEngine::IsInitialized();

		float dt = Application::GetInstance() ? Application::GetInstance()->GetTime().GetDeltaTime() : 0.0f;

		auto view = scene.GetRegistry().view<ScriptComponent>(entt::exclude<DisabledTag>);

		for (auto [entity, scriptComp] : view.each())
		{
			for (auto& instance : scriptComp.Scripts)
			{
				if (instance.GetClassName().empty()) continue;

				if (!instance.IsBound())
					instance.Bind(entity);

				if (!instance.HasAnyInstance())
				{
					const ScriptType scriptType = instance.GetType();
					const bool canUseManaged = scriptType == ScriptType::Managed || scriptType == ScriptType::Unknown;
					const bool canUseNative = scriptType == ScriptType::Native || scriptType == ScriptType::Unknown;

					// Try C# (managed) first
					if (canUseManaged && managedRuntimeReady && ScriptEngine::ClassExists(instance.GetClassName()))
					{
						uint32_t handle = ScriptEngine::CreateScriptInstance(
							instance.GetClassName(), entity);
						if (handle != 0)
							instance.SetGCHandle(handle);
					}
					// Then try native C++
					if (!instance.HasAnyInstance()
						&& canUseNative
						&& !IsTaskRunning(m_NativeRebuildTask)
						&& m_NativeHost.IsLoaded())
					{
						NativeScript* native = m_NativeHost.CreateInstance(
							instance.GetClassName(), entity, &scene);
						if (native)
							instance.SetNativePtr(native);
					}

					if (!instance.HasAnyInstance())
						continue;

					// Apply pending [ShowInEditor] field values from deserialization
					if (instance.HasManagedInstance() && !scriptComp.PendingFieldValues.empty()) {
						std::string prefix = instance.GetClassName() + ".";
						auto& callbacks = ScriptEngine::GetCallbacks();
						if (callbacks.SetScriptField) {
							for (auto it = scriptComp.PendingFieldValues.begin(); it != scriptComp.PendingFieldValues.end(); ) {
								if (it->first.rfind(prefix, 0) == 0) {
									std::string fieldName = it->first.substr(prefix.size());
									callbacks.SetScriptField(
										static_cast<int32_t>(instance.GetGCHandle()),
										fieldName.c_str(), it->second.c_str());
									it = scriptComp.PendingFieldValues.erase(it);
								} else {
									++it;
								}
							}
						}
					}
				}

				// Dispatch lifecycle based on script type
				if (instance.GetType() == ScriptType::Managed)
				{
					if (!managedRuntimeReady || !instance.HasManagedInstance()) {
						continue;
					}

					if (!instance.HasEnabled())
					{
						ScriptEngine::InvokeOnEnable(instance.GetGCHandle());
						instance.MarkEnabled();
					}

					if (!instance.HasStarted())
					{
						instance.MarkStarted();
						ScriptEngine::InvokeStart(instance.GetGCHandle());
					}
					ScriptEngine::InvokeUpdate(instance.GetGCHandle());
				}
				else if (instance.GetType() == ScriptType::Native)
				{
					if (IsTaskRunning(m_NativeRebuildTask) || !instance.HasNativeInstance()) {
						continue;
					}

					NativeScript* nativeInstance = instance.GetNativePtr();
					if (!nativeInstance) {
						instance.Unbind();
						continue;
					}

					if (!instance.HasStarted())
					{
						if (!TryInvokeNativeScript(instance, "Start", [nativeInstance]() {
							nativeInstance->Start();
						})) {
							m_NativeHost.DestroyInstance(nativeInstance);
							instance.Unbind();
							continue;
						}
						instance.MarkStarted();
					}

					if (!TryInvokeNativeScript(instance, "Update", [nativeInstance, dt]() {
						nativeInstance->Update(dt);
					})) {
						m_NativeHost.DestroyInstance(nativeInstance);
						instance.Unbind();
						continue;
					}
				}
			}
		}
	}

	void ScriptSystem::OnDestroy(Scene& scene)
	{
		if (m_PollingOwner == this) {
			m_PollingOwner = nullptr;
		}

		if (ScriptEngine::IsInitialized()) {
			TeardownManagedScripts(scene);
		}
		TeardownNativeScripts(scene);

		if (m_ActiveSystemCount > 0) {
			--m_ActiveSystemCount;
		}
		if (m_ActiveSystemCount > 0) {
			return;
		}

		m_ScriptWatcher.Stop();
		m_NativeWatcher.Stop();

		AbandonTask(m_RebuildTask, "Managed script rebuild");
		AbandonTask(m_NativeRebuildTask, "Native script rebuild");
		m_RebuildQueued = false;
		m_NativeRebuildQueued = false;

		if (Application::IsShuttingDown() && ScriptEngine::IsInitialized()) {
			ScriptEngine::Shutdown();
		}

		m_NativeHost.UnloadDLL();
		m_LastScene = nullptr;
		m_CoreAssemblyPath.clear();
		m_UserAssemblyPath.clear();
		m_SandboxProjectPath.clear();
		m_NativeProjectDirectory.clear();
		m_NativeSourceDirectory.clear();
		m_NativeDLLPath.clear();
		m_NativeTargetName.clear();
		m_SuppressRecompile = false;
		m_PollingOwner = nullptr;
	}

} // namespace Bolt
