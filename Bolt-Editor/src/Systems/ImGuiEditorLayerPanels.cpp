#include <pch.hpp>
#include "Systems/ImGuiEditorLayer.hpp"

#include <imgui.h>

#include "Core/Application.hpp"
#include "Core/Window.hpp"
#include "Graphics/TextureManager.hpp"
#include "Gui/EditorIcons.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Packages/GitHubSource.hpp"
#include "Packages/NuGetSource.hpp"
#include "Project/BoltProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Serialization/Directory.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Utils/Process.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace Bolt {
	namespace {
		bool NeedsCopy(const std::filesystem::path& src, const std::filesystem::path& dest) {
			if (!std::filesystem::exists(dest)) return true;
			try {
				return std::filesystem::last_write_time(src) > std::filesystem::last_write_time(dest);
			}
			catch (...) {
				return true;
			}
		}

		int CopyDirIncremental(const std::filesystem::path& srcDir, const std::filesystem::path& destDir) {
			int copied = 0;
			std::filesystem::create_directories(destDir);
			for (auto& entry : std::filesystem::recursive_directory_iterator(srcDir)) {
				auto rel = std::filesystem::relative(entry.path(), srcDir);
				auto dest = destDir / rel;
				try {
					if (entry.is_directory()) {
						std::filesystem::create_directories(dest);
					}
					else if (NeedsCopy(entry.path(), dest)) {
						std::filesystem::create_directories(dest.parent_path());
						std::filesystem::copy_file(entry.path(), dest, std::filesystem::copy_options::overwrite_existing);
						copied++;
					}
				}
				catch (...) {
				}
			}
			return copied;
		}

		std::string GetRuntimeExecutableFilename() {
#if defined(BT_PLATFORM_WINDOWS)
			return "Bolt-Runtime.exe";
#else
			return "Bolt-Runtime";
#endif
		}

		std::string GetEngineRuntimeFilename() {
#if defined(BT_PLATFORM_WINDOWS)
			return "Bolt-Engine.dll";
#elif defined(__APPLE__)
			return "libBolt-Engine.dylib";
#else
			return "libBolt-Engine.so";
#endif
		}

		std::string GetNetHostRuntimeFilename() {
#if defined(BT_PLATFORM_WINDOWS)
			return "nethost.dll";
#else
			return "libnethost.so";
#endif
		}

		std::string NormalizePreviewTexturePath(const std::filesystem::path& path) {
			std::error_code ec;
			std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
			if (ec) {
				ec.clear();
				normalized = std::filesystem::absolute(path, ec);
				if (ec) {
					normalized = path.lexically_normal();
				}
			}

			return normalized.lexically_normal().make_preferred().string();
		}
	}

	void ImGuiEditorLayer::RenderLogPanel() {
		DrainPendingLogEntries();
		ImGui::Begin("Log");

		if (ImGui::Button("Clear")) {
			ClearLogEntries();
		}

		int infoCount = 0, warnCount = 0, errorCount = 0;
		for (const auto& entry : m_LogEntries) {
			if (entry.Level <= Log::Level::Info) infoCount++;
			else if (entry.Level == Log::Level::Warn) warnCount++;
			else errorCount++;
		}

		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();

		{
			unsigned int infoIcon = EditorIcons::Get("info", 16);
			if (infoIcon) {
				ImVec4 tint = m_ShowLogInfo ? ImVec4(1, 1, 1, 1) : ImVec4(0.4f, 0.4f, 0.4f, 0.5f);
				if (ImGui::ImageButton("##FilterInfo",
					static_cast<ImTextureID>(static_cast<intptr_t>(infoIcon)),
					ImVec2(14, 14), ImVec2(0, 1), ImVec2(1, 0), ImVec4(0, 0, 0, 0), tint)) {
					m_ShowLogInfo = !m_ShowLogInfo;
				}
			}
			else if (ImGui::SmallButton(m_ShowLogInfo ? "[I]" : "( )")) {
				m_ShowLogInfo = !m_ShowLogInfo;
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Info (%d)", infoCount);
		}

		ImGui::SameLine();

		{
			unsigned int warnIcon = EditorIcons::Get("warning", 16);
			ImVec4 tint = m_ShowLogWarn ? ImVec4(1, 1, 1, 1) : ImVec4(0.4f, 0.4f, 0.4f, 0.5f);
			if (warnIcon) {
				if (ImGui::ImageButton("##FilterWarn",
					static_cast<ImTextureID>(static_cast<intptr_t>(warnIcon)),
					ImVec2(14, 14), ImVec2(0, 1), ImVec2(1, 0), ImVec4(0, 0, 0, 0), tint)) {
					m_ShowLogWarn = !m_ShowLogWarn;
				}
			}
			else {
				ImGui::PushStyleColor(ImGuiCol_Text, tint);
				if (ImGui::SmallButton(m_ShowLogWarn ? "W" : "(W)")) {
					m_ShowLogWarn = !m_ShowLogWarn;
				}
				ImGui::PopStyleColor();
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Warnings (%d)", warnCount);
		}

		ImGui::SameLine();

		{
			unsigned int errIcon = EditorIcons::Get("error", 16);
			ImVec4 tint = m_ShowLogError ? ImVec4(1, 1, 1, 1) : ImVec4(0.4f, 0.4f, 0.4f, 0.5f);
			if (errIcon) {
				if (ImGui::ImageButton("##FilterError",
					static_cast<ImTextureID>(static_cast<intptr_t>(errIcon)),
					ImVec2(14, 14), ImVec2(0, 1), ImVec2(1, 0), ImVec4(0, 0, 0, 0), tint)) {
					m_ShowLogError = !m_ShowLogError;
				}
			}
			else {
				ImGui::PushStyleColor(ImGuiCol_Text, tint);
				if (ImGui::SmallButton(m_ShowLogError ? "E" : "(E)")) {
					m_ShowLogError = !m_ShowLogError;
				}
				ImGui::PopStyleColor();
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Errors (%d)", errorCount);
		}

		ImGui::Separator();

		ImGui::BeginChild("LogScroll");
		const bool stickToBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImGuiStyle& style = ImGui::GetStyle();
		for (int i = 0; i < static_cast<int>(m_LogEntries.size()); i++) {
			const LogEntry& entry = m_LogEntries[i];
			if (entry.Level <= Log::Level::Info && !m_ShowLogInfo) continue;
			if (entry.Level == Log::Level::Warn && !m_ShowLogWarn) continue;
			if (entry.Level >= Log::Level::Error && !m_ShowLogError) continue;

			ImVec4 color = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
			if (entry.Level == Log::Level::Warn) color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
			else if (entry.Level >= Log::Level::Error) color = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);

			ImGui::PushID(i);
			const float rowWidth = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
			const float wrapWidth = std::max(rowWidth - style.FramePadding.x * 2.0f, 1.0f);
			const ImVec2 textSize = ImGui::CalcTextSize(entry.Message.c_str(), nullptr, false, wrapWidth);
			const float rowHeight = std::max(textSize.y, ImGui::GetTextLineHeight()) + style.FramePadding.y * 2.0f;
			const ImVec2 rowMin = ImGui::GetCursorScreenPos();
			const ImVec2 rowMax(rowMin.x + rowWidth, rowMin.y + rowHeight);

			ImGui::InvisibleButton("##LogEntry", ImVec2(rowWidth, rowHeight));
			if (ImGui::IsItemHovered()) {
				drawList->AddRectFilled(rowMin, rowMax, IM_COL32(70, 78, 92, 120), 4.0f);
			}

			if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
				ImGui::SetClipboardText(entry.Message.c_str());
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Click to copy");
			}

			if (ImGui::BeginPopupContextItem("##LogCtx")) {
				if (ImGui::MenuItem("Copy")) {
					ImGui::SetClipboardText(entry.Message.c_str());
				}
				if (ImGui::MenuItem("Copy All Visible")) {
					std::string all;
					for (const auto& visibleEntry : m_LogEntries) {
						if (visibleEntry.Level <= Log::Level::Info && !m_ShowLogInfo) continue;
						if (visibleEntry.Level == Log::Level::Warn && !m_ShowLogWarn) continue;
						if (visibleEntry.Level >= Log::Level::Error && !m_ShowLogError) continue;
						all += visibleEntry.Message + "\n";
					}
					ImGui::SetClipboardText(all.c_str());
				}
				ImGui::EndPopup();
			}

			ImGui::SetCursorScreenPos(ImVec2(rowMin.x + style.FramePadding.x, rowMin.y + style.FramePadding.y));
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::PushTextWrapPos(rowMin.x + rowWidth - style.FramePadding.x);
			ImGui::TextUnformatted(entry.Message.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
			ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMin.y + rowHeight));
			ImGui::PopID();
		}

		if (stickToBottom) {
			ImGui::SetScrollHereY(1.0f);
		}

		ImGui::EndChild();
		ImGui::End();
	}

	void ImGuiEditorLayer::RenderProjectPanel() {
		if (!m_AssetBrowserInitialized) {
			std::string assetsRoot;
			BoltProject* project = ProjectManager::GetCurrentProject();
			if (project) assetsRoot = project->AssetsDirectory;
			else assetsRoot = Path::Combine(Path::ExecutableDir(), "Assets");

			if (!Directory::Exists(assetsRoot)) {
				Directory::Create(assetsRoot);
			}

			m_AssetBrowser.Initialize(assetsRoot);
			m_AssetBrowserInitialized = true;
		}

		m_AssetBrowser.Render();
		if (m_AssetBrowser.TakeSelectionActivated() && !m_AssetBrowser.GetSelectedPath().empty()) {
			ClearEntitySelection();
		}
	}

	void ImGuiEditorLayer::ExecuteBuild() {
		m_BuildStartTime = std::chrono::steady_clock::now();

		BoltProject* project = ProjectManager::GetCurrentProject();
		if (!project) { BT_ERROR_TAG("Build", "No project loaded."); return; }

		m_BuildOutputDir = std::string(m_BuildOutputDirBuffer);
		if (m_BuildOutputDir.empty()) { BT_ERROR_TAG("Build", "No output directory specified."); return; }

		BT_INFO_TAG("Build", "Starting build for '{}'...", project->Name);

		Scene* active = SceneManager::Get().GetActiveScene();
		if (active && active->IsDirty()) {
			std::string scenePath = project->GetSceneFilePath(active->GetName());
			SceneSerializer::SaveToFile(*active, scenePath);
			project->LastOpenedScene = active->GetName();
			project->Save();
			BT_INFO_TAG("Build", "Saved current scene.");
		}

		auto exeDir = std::filesystem::path(Path::ExecutableDir());
		auto outDir = std::filesystem::path(m_BuildOutputDir);
		const std::string buildConfiguration = BoltProject::GetActiveBuildConfiguration();

		if (std::filesystem::exists(project->CsprojPath)) {
			BT_INFO_TAG("Build", "Compiling C# scripts...");
			Process::Result buildResult = Process::Run({
				"dotnet",
				"build",
				project->CsprojPath,
				"-c", buildConfiguration,
				"--nologo",
				"-v", "q",
				"-p:DefineConstants=" + BoltProject::BuildManagedDefineConstants("BOLT_BUILD")
			});
			if (!buildResult.Succeeded()) {
				BT_ERROR_TAG("Build", "C# script compilation failed (exit code {}).", buildResult.ExitCode);
				if (!buildResult.Output.empty()) {
					BT_ERROR_TAG("Build", "{}", buildResult.Output);
				}
				return;
			}
			BT_INFO_TAG("Build", "C# scripts compiled.");
		}

		try {
			std::filesystem::create_directories(outDir);
		}
		catch (const std::exception& e) {
			BT_ERROR_TAG("Build", "Failed to create output directory: {}", e.what());
			return;
		}

		auto copyFile = [&](const std::filesystem::path& src, const std::filesystem::path& dest, const std::string& name) {
			try {
				if (!std::filesystem::exists(src)) {
					BT_WARN_TAG("Build", "{} not found at {}", name, src.string());
					return;
				}
				auto canonical = std::filesystem::canonical(src);
				if (NeedsCopy(canonical, dest)) {
					std::filesystem::create_directories(dest.parent_path());
					std::filesystem::copy_file(canonical, dest, std::filesystem::copy_options::overwrite_existing);
					BT_INFO_TAG("Build", "Copied {}", name);
				}
			}
			catch (const std::exception& e) {
				BT_WARN_TAG("Build", "Failed to copy {}: {}", name, e.what());
			}
		};

		const std::filesystem::path runtimeOutputDirectory = exeDir / ".." / "Bolt-Runtime";
		const std::string runtimeExecutableFilename = GetRuntimeExecutableFilename();
		copyFile(runtimeOutputDirectory / runtimeExecutableFilename,
			outDir / (project->Name + std::filesystem::path(runtimeExecutableFilename).extension().string()),
			"runtime executable");
		copyFile(runtimeOutputDirectory / GetEngineRuntimeFilename(),
			outDir / GetEngineRuntimeFilename(),
			GetEngineRuntimeFilename());

		auto scriptCoreDir = exeDir / ".." / "Bolt-ScriptCore";
		copyFile(scriptCoreDir / "Bolt-ScriptCore.dll", outDir / "Bolt-ScriptCore.dll", "Bolt-ScriptCore.dll");
		copyFile(scriptCoreDir / "Bolt-ScriptCore.runtimeconfig.json", outDir / "Bolt-ScriptCore.runtimeconfig.json", "ScriptCore config");
		copyFile(scriptCoreDir / "Bolt-ScriptCore.deps.json", outDir / "Bolt-ScriptCore.deps.json", "ScriptCore deps");
		{
			const std::string netHostFilename = GetNetHostRuntimeFilename();
			const std::filesystem::path netHostSource = exeDir / netHostFilename;
			if (std::filesystem::exists(netHostSource)) {
				copyFile(netHostSource, outDir / netHostFilename, netHostFilename);
			}
		}

		try {
			std::filesystem::copy_file(project->ProjectFilePath, outDir / "bolt-project.json",
				std::filesystem::copy_options::overwrite_existing);
		}
		catch (const std::exception& e) {
			BT_WARN_TAG("Build", "Failed to copy bolt-project.json: {}", e.what());
		}

		if (std::filesystem::exists(project->AssetsDirectory)) {
			int updatedFiles = CopyDirIncremental(project->AssetsDirectory, outDir / "Assets");
			BT_INFO_TAG("Build", "Assets: {} file(s) updated", updatedFiles);
		}

		{
			std::string boltAssetsSrc;
			if (std::filesystem::exists(project->BoltAssetsDirectory)) {
				boltAssetsSrc = project->BoltAssetsDirectory;
			}
			else {
				boltAssetsSrc = Path::ResolveBoltAssets("");
			}

			if (!boltAssetsSrc.empty() && std::filesystem::exists(boltAssetsSrc)) {
				int updatedFiles = CopyDirIncremental(boltAssetsSrc, outDir / "BoltAssets");
				BT_INFO_TAG("Build", "BoltAssets: {} file(s) updated", updatedFiles);
			}
			else {
				BT_WARN_TAG("Build", "BoltAssets not found - build may be incomplete");
			}
		}

		{
			auto userBinDir = std::filesystem::path(project->GetUserAssemblyOutputPath()).parent_path();
			if (std::filesystem::exists(userBinDir)) {
				auto destBinDir = outDir / "bin" / buildConfiguration;
				int copied = 0;
				for (const auto& entry : std::filesystem::directory_iterator(userBinDir)) {
					if (!entry.is_regular_file()) continue;
					auto ext = entry.path().extension().string();
					if (ext == ".dll" || ext == ".json") {
						copyFile(entry.path(), destBinDir / entry.path().filename(), entry.path().filename().string());
						copied++;
					}
				}
				BT_INFO_TAG("Build", "User assemblies: {} file(s) copied", copied);
			}
		}

		{
			std::string nativeDll = project->GetNativeDllPath();
			if (std::filesystem::exists(nativeDll)) {
				const std::filesystem::path nativeLibraryPath(nativeDll);
				copyFile(nativeDll,
					outDir / "NativeScripts" / "build" / buildConfiguration / nativeLibraryPath.filename(),
					"native script DLL");
			}
		}

		float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_BuildStartTime).count();
		BT_INFO_TAG("Build", "Build completed in {:.2f}s -> {}", elapsed, m_BuildOutputDir);

#ifdef BT_PLATFORM_WINDOWS
		ShellExecuteA(nullptr, "open", outDir.string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
	}

	void ImGuiEditorLayer::RenderBuildPanel() {
		if (!m_ShowBuildPanel) return;

		ImGui::Begin("Build", &m_ShowBuildPanel);

		BoltProject* project = ProjectManager::GetCurrentProject();
		if (project) {
			if (!m_BuildSceneListInitialized) {
				m_BuildSceneList.clear();
				auto scenesDir = std::filesystem::path(project->ScenesDirectory);
				if (std::filesystem::exists(scenesDir)) {
					for (const auto& entry : std::filesystem::directory_iterator(scenesDir)) {
						if (!entry.is_regular_file()) continue;
						if (entry.path().extension() == ".scene") {
							m_BuildSceneList.push_back(entry.path().stem().string());
						}
					}
				}

				if (!project->StartupScene.empty()) {
					auto it = std::find(m_BuildSceneList.begin(), m_BuildSceneList.end(), project->StartupScene);
					if (it != m_BuildSceneList.end() && it != m_BuildSceneList.begin()) {
						std::string startupScene = *it;
						m_BuildSceneList.erase(it);
						m_BuildSceneList.insert(m_BuildSceneList.begin(), startupScene);
					}
				}
				m_BuildSceneListInitialized = true;
			}

			if (ImGui::CollapsingHeader("Scene List", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Indent(8);
				if (m_BuildSceneList.empty()) {
					ImGui::TextDisabled("No scenes found in Assets/Scenes/");
				}
				else {
					for (int i = 0; i < static_cast<int>(m_BuildSceneList.size()); i++) {
						ImGui::PushID(i);
						bool isStartup = (i == 0);

						if (isStartup) ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "[Startup]");
						else ImGui::TextDisabled("[%d]", i);

						ImGui::SameLine();
						const std::string sceneItemId = std::to_string(i);
						ImGuiUtils::SelectableEllipsis(m_BuildSceneList[i], sceneItemId.c_str());

						if (ImGui::BeginDragDropSource()) {
							ImGui::SetDragDropPayload("SCENE_LIST_ITEM", &i, sizeof(int));
							ImGui::Text("Move: %s", m_BuildSceneList[i].c_str());
							ImGui::EndDragDropSource();
						}
						if (ImGui::BeginDragDropTarget()) {
							if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_LIST_ITEM")) {
								int srcIndex = *static_cast<const int*>(payload->Data);
								if (srcIndex != i) {
									std::string moved = m_BuildSceneList[srcIndex];
									m_BuildSceneList.erase(m_BuildSceneList.begin() + srcIndex);
									int insertAt = (srcIndex < i) ? i - 1 : i;
									m_BuildSceneList.insert(m_BuildSceneList.begin() + insertAt, moved);
									if (!m_BuildSceneList.empty()) {
										project->StartupScene = m_BuildSceneList[0];
										project->Save();
									}
								}
							}
							ImGui::EndDragDropTarget();
						}

						ImGui::PopID();
					}
				}

				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
						std::string droppedPath(static_cast<const char*>(payload->Data));
						if (std::filesystem::path(droppedPath).extension() == ".scene") {
							std::string sceneName = std::filesystem::path(droppedPath).stem().string();
							auto it = std::find(m_BuildSceneList.begin(), m_BuildSceneList.end(), sceneName);
							if (it == m_BuildSceneList.end()) {
								m_BuildSceneList.push_back(sceneName);
							}
						}
					}
					ImGui::EndDragDropTarget();
				}

				ImGui::Unindent(8);
			}

			ImGui::Spacing();

			if (ImGui::CollapsingHeader("Build Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Indent(8);
				if (ImGui::Button("Open Player Settings")) {
					m_ShowPlayerSettings = true;
				}

				ImGui::Spacing();
				ImGui::Text("Output Directory:");
				ImGui::SetNextItemWidth(-1);
				ImGui::InputText("##BuildOutputDir", m_BuildOutputDirBuffer, sizeof(m_BuildOutputDirBuffer));
				ImGui::Unindent(8);
			}
		}

		ImGui::Spacing();

		bool canBuild = project && !Application::GetIsPlaying() && m_BuildState == 0;
		if (!canBuild) ImGui::BeginDisabled();

		float halfWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
		if (ImGui::Button("Build", ImVec2(halfWidth, 0))) {
			m_BuildState = 1;
			m_BuildAndPlay = false;
			m_BuildStartTime = std::chrono::steady_clock::now();
		}
		ImGui::SameLine();
		if (ImGui::Button("Build and Play", ImVec2(-1, 0))) {
			m_BuildState = 1;
			m_BuildAndPlay = true;
			m_BuildStartTime = std::chrono::steady_clock::now();
		}

		if (!canBuild) ImGui::EndDisabled();
		ImGui::End();
	}

	void ImGuiEditorLayer::RenderPlayerSettingsPanel() {
		if (!m_ShowPlayerSettings) return;

		ImGui::Begin("Player Settings", &m_ShowPlayerSettings);
		BoltProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			ImGui::TextDisabled("No project loaded");
			ImGui::End();
			return;
		}

		bool changed = false;
		if (ImGui::CollapsingHeader("Resolution", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Indent(8);
			changed |= ImGui::InputInt("Width", &project->BuildWidth);
			changed |= ImGui::InputInt("Height", &project->BuildHeight);
			if (project->BuildWidth < 320) project->BuildWidth = 320;
			if (project->BuildHeight < 240) project->BuildHeight = 240;
			ImGui::Unindent(8);
		}

		if (ImGui::CollapsingHeader("Window", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Indent(8);
			changed |= ImGui::Checkbox("Fullscreen", &project->BuildFullscreen);
			changed |= ImGui::Checkbox("Resizable", &project->BuildResizable);
			ImGui::Unindent(8);
		}

		if (ImGui::CollapsingHeader("App Icon", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Indent(8);

			if (!project->AppIconPath.empty()) {
				TextureHandle iconHandle = TextureManager::LoadTexture(project->AppIconPath);
				Texture2D* iconTex = TextureManager::GetTexture(iconHandle);
				if (iconTex && iconTex->IsValid()) {
					ImGui::Image(
						static_cast<ImTextureID>(static_cast<intptr_t>(iconTex->GetHandle())),
						ImVec2(64, 64), ImVec2(0, 1), ImVec2(1, 0));
					ImGui::SameLine();
				}
				else {
					ImGui::TextDisabled("Failed to load:");
					ImGuiUtils::TextDisabledEllipsis(project->AppIconPath);
				}

				if (ImGui::Button("Clear")) {
					project->AppIconPath.clear();
					changed = true;
					Application::GetInstance()->GetWindow()->SetWindowIconFromResource();
				}

				if (iconTex && iconTex->IsValid()) {
					ImGuiUtils::TextDisabledEllipsis(project->AppIconPath);
				}
			}
			else {
				ImGui::TextDisabled("No icon set");
				ImGui::TextDisabled("Drag an image from the Asset Browser");
			}

			if (ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
					std::string droppedPath(static_cast<const char*>(payload->Data));
					std::string ext = std::filesystem::path(droppedPath).extension().string();
					std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

					if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") {
						std::filesystem::path absPath(droppedPath);
						std::filesystem::path assetsDir(project->AssetsDirectory);
						if (absPath.string().find(assetsDir.string()) == 0) {
							project->AppIconPath = std::filesystem::relative(absPath, assetsDir.parent_path()).string();
						}
						else {
							project->AppIconPath = absPath.filename().string();
						}
						changed = true;

						TextureHandle icon = TextureManager::LoadTexture(project->AppIconPath);
						Texture2D* tex = TextureManager::GetTexture(icon);
						if (tex && tex->IsValid()) {
							Application::GetInstance()->GetWindow()->SetWindowIcon(tex);
						}
					}
				}
				ImGui::EndDragDropTarget();
			}

			ImGui::Unindent(8);
		}

		if (changed) {
			project->Save();
		}

		ImGui::End();
	}

	void ImGuiEditorLayer::RenderAssetInspector() {
		const std::string& selectedPath = m_AssetBrowser.GetSelectedPath();
		if (selectedPath.empty()) {
			ImGui::TextDisabled("No entity or asset selected");
			return;
		}

		std::filesystem::path path(selectedPath);
		if (!std::filesystem::exists(path)) {
			ImGui::TextDisabled("No entity or asset selected");
			return;
		}

		std::string name = path.filename().string();
		std::string ext = path.extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

		ImGui::TextDisabled("Asset:");
		ImGui::SameLine();
		ImGuiUtils::TextEllipsis(name);
		ImGui::Separator();

		ImGui::TextDisabled("Path:");
		ImGui::SameLine();
		ImGuiUtils::TextDisabledEllipsis(selectedPath);

		try {
			auto fileSize = std::filesystem::file_size(path);
			if (fileSize >= 1024 * 1024) ImGui::TextDisabled("Size: %.2f MB", fileSize / (1024.0f * 1024.0f));
			else if (fileSize >= 1024) ImGui::TextDisabled("Size: %.1f KB", fileSize / 1024.0f);
			else ImGui::TextDisabled("Size: %llu bytes", fileSize);
		}
		catch (...) {
		}

		ImGui::TextDisabled("Type: %s", ext.c_str());
		if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
			ImGui::Spacing();
			const Texture2D* tex = GetPreviewTexture(path);
			if (tex && tex->IsValid()) {
				ImGuiUtils::DrawTexturePreview(tex->GetHandle(), tex->GetWidth(), tex->GetHeight(), 128.0f);
				ImGui::Text("%.0f x %.0f", tex->GetWidth(), tex->GetHeight());
			}
		}
	}

	const Texture2D* ImGuiEditorLayer::GetPreviewTexture(const std::filesystem::path& path) {
		const std::string canonicalPath = NormalizePreviewTexturePath(path);
		if (canonicalPath.empty()) {
			return nullptr;
		}

		if (const auto it = m_PreviewTextureLookup.find(canonicalPath); it != m_PreviewTextureLookup.end()) {
			PreviewTextureEntry& entry = m_PreviewTextureCache[it->second];
			entry.LastTouchTick = ++m_PreviewTextureTick;
			return entry.Texture.get();
		}

		auto texture = std::make_unique<Texture2D>(canonicalPath.c_str(), Filter::Point, Wrap::Clamp, Wrap::Clamp);
		if (!texture->IsValid()) {
			return nullptr;
		}

		PreviewTextureEntry entry;
		entry.CanonicalPath = canonicalPath;
		entry.Texture = std::move(texture);
		entry.LastTouchTick = ++m_PreviewTextureTick;

		const size_t index = m_PreviewTextureCache.size();
		m_PreviewTextureLookup[canonicalPath] = index;
		m_PreviewTextureCache.push_back(std::move(entry));
		TrimPreviewTextureCache();
		return m_PreviewTextureCache.back().Texture.get();
	}

	void ImGuiEditorLayer::TrimPreviewTextureCache() {
		while (m_PreviewTextureCache.size() > kMaxPreviewTextures) {
			auto victimIt = std::min_element(
				m_PreviewTextureCache.begin(),
				m_PreviewTextureCache.end(),
				[](const PreviewTextureEntry& a, const PreviewTextureEntry& b) {
					return a.LastTouchTick < b.LastTouchTick;
				});
			if (victimIt == m_PreviewTextureCache.end()) {
				break;
			}

			m_PreviewTextureCache.erase(victimIt);
			m_PreviewTextureLookup.clear();
			for (size_t i = 0; i < m_PreviewTextureCache.size(); ++i) {
				m_PreviewTextureLookup[m_PreviewTextureCache[i].CanonicalPath] = i;
			}
		}
	}

	void ImGuiEditorLayer::ClearPreviewTextureCache() {
		m_PreviewTextureLookup.clear();
		m_PreviewTextureCache.clear();
		m_PreviewTextureTick = 0;
	}

	void ImGuiEditorLayer::RenderPackageManagerPanel() {
		if (!m_ShowPackageManager) return;

		if (!m_PackageManagerInitialized) {
			m_PackageManager.Initialize();

			if (m_PackageManager.IsReady()) {
				m_PackageManager.AddSource(std::make_unique<NuGetSource>(m_PackageManager.GetToolPath()));
				m_PackageManager.AddSource(std::make_unique<GitHubSource>(
					m_PackageManager.GetToolPath(),
					"https://raw.githubusercontent.com/Ben-Scr/bolt-packages/main/index.json",
					"Engine Packages"));
			}

			m_PackageManagerPanel.Initialize(&m_PackageManager);
			m_PackageManagerInitialized = true;
		}

		ImGui::Begin("Package Manager", &m_ShowPackageManager);
		m_PackageManagerPanel.Render();
		ImGui::End();
	}

} // namespace Bolt
