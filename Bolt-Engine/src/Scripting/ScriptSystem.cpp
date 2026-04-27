#include "pch.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/NativeScript.hpp"
#include "Scene/Scene.hpp"
#include "Components/Tags.hpp"
#include "Core/Log.hpp"
#include "Core/Application.hpp"
#include "Serialization/Path.hpp"
#include "Project/ProjectManager.hpp"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <optional>

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
			if (className.empty() || scriptComp.HasScript(className)) {
				return;
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

		void TeardownManagedScriptInstance(Scene& scene, ScriptInstance& instance)
		{
			if (!instance.HasManagedInstance() || !ScriptEngine::IsInitialized()) {
				return;
			}

			ScriptEngine::SetScene(&scene);
			ScriptEngine::InvokeOnDestroy(instance.GetGCHandle());
			ScriptEngine::DestroyScriptInstance(instance.GetGCHandle());
			instance.Unbind();
		}

		void TeardownNativeScriptInstance(Scene& scene, ScriptInstance& instance, NativeScriptHost& nativeHost)
		{
			if (!instance.HasNativeInstance()) {
				return;
			}

			ScriptEngine::SetScene(&scene);
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

		ScriptEngine::Init();

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

		if (std::filesystem::exists(m_CoreAssemblyPath))
			ScriptEngine::LoadCoreAssembly(m_CoreAssemblyPath);

		// User assembly — project-aware
		if (m_UserAssemblyPath.empty())
		{
			if (project)
			{
				auto userDll = std::filesystem::path(project->GetUserAssemblyOutputPath());
				if (std::filesystem::exists(userDll))
					m_UserAssemblyPath = userDll.string();
			}
			else
			{
				auto sandboxPath = exeDir / ".." / "Bolt-Sandbox" / "Bolt-Sandbox.dll";
				if (std::filesystem::exists(sandboxPath))
					m_UserAssemblyPath = sandboxPath.string();
			}
		}

		if (!m_UserAssemblyPath.empty() && std::filesystem::exists(m_UserAssemblyPath))
			ScriptEngine::LoadUserAssembly(m_UserAssemblyPath);

		// C# file watcher — project-aware
		if (project)
		{
			auto scriptsDir = std::filesystem::path(project->ScriptsDirectory);
			auto csproj = std::filesystem::path(project->CsprojPath);
			if (std::filesystem::exists(csproj))
			{
				m_SandboxProjectPath = std::filesystem::canonical(csproj).string();
				m_ScriptWatcher.Watch(
					BuildManagedWatchTargets(scriptsDir, csproj, std::filesystem::path(project->SlnPath)),
					GetManagedWatchPatterns(),
					[this]() { RebuildAndReloadScripts(); });
			}
		}
		else
		{
			auto sandboxProjectDir = exeDir / ".." / ".." / ".." / "Bolt-Sandbox";
			auto sandboxSourceDir = sandboxProjectDir / "Source";
			auto sandboxCsproj = sandboxProjectDir / "Bolt-Sandbox.csproj";
			if (std::filesystem::exists(sandboxCsproj))
			{
				m_SandboxProjectPath = std::filesystem::canonical(sandboxCsproj).string();
				m_ScriptWatcher.Watch(
					BuildManagedWatchTargets(sandboxSourceDir, sandboxCsproj),
					GetManagedWatchPatterns(),
					[this]() { RebuildAndReloadScripts(); });
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
				m_NativeWatcher.Watch(
					BuildNativeWatchTargets(std::filesystem::path(m_NativeProjectDirectory), std::filesystem::path(m_NativeSourceDirectory)),
					GetNativeWatchPatterns(),
					[this]() { RebuildAndReloadNativeScripts(); });
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
				m_NativeWatcher.Watch(
					BuildNativeWatchTargets(std::filesystem::path(m_NativeProjectDirectory), std::filesystem::path(m_NativeSourceDirectory)),
					GetNativeWatchPatterns(),
					[this]() { RebuildAndReloadNativeScripts(); });
			}
		}

		BT_INFO_TAG("ScriptSystem", "Script system initialized");
	}

	// ── C# rebuild ─────────────────────────────────────────────────────

	void ScriptSystem::RebuildAndReloadScripts()
	{
		if (m_RebuildTask) {
			return;
		}
		if (m_SandboxProjectPath.empty()) {
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
		if (m_NativeRebuildTask || m_NativeProjectDirectory.empty()) {
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

			Process::Result configureResult{};
			if (NativeBuildNeedsConfigure(nativeProjectDirectory, buildDirectory)) {
				std::vector<std::string> configureCommand = {
					"cmake",
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
				"cmake",
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

		ScriptEngine::SetScene(&scene);

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
		ScriptEngine::SetScene(&scene);

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
				ScriptEngine::ReloadAssemblies();
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
		ScriptEngine::SetScene(&scene);
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
					// Try C# (managed) first
					if (managedRuntimeReady && ScriptEngine::ClassExists(instance.GetClassName()))
					{
						uint32_t handle = ScriptEngine::CreateScriptInstance(
							instance.GetClassName(), entity);
						if (handle != 0)
							instance.SetGCHandle(handle);
					}
					// Then try native C++
					else if (!IsTaskRunning(m_NativeRebuildTask) && m_NativeHost.IsLoaded())
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

		if (ScriptEngine::IsInitialized()) {
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
