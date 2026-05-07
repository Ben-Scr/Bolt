#include <pch.hpp>
#include "Systems/ImGuiEditorLayer.hpp"

#include <imgui.h>
#include <glad/glad.h>

#include <cstdio>

#include "Components/Components.hpp"
#include "Core/Application.hpp"
#include "Core/Window.hpp"
#include "Graphics/OpenGL.hpp"
#include "Graphics/Renderer2D.hpp"
#include "Graphics/GizmoRenderer.hpp"
#include "Graphics/Gizmo.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/ComponentRegistry.hpp"
#include <Scene/EntityHelper.hpp>

#include "Graphics/TextureManager.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Serialization/Path.hpp"
#include "Project/ProjectManager.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/File.hpp"
#include "Utils/Process.hpp"
#include "Packages/NuGetSource.hpp"
#include "Packages/GitHubSource.hpp"
#include "Editor/ExternalEditor.hpp"
#include "Gui/EditorIcons.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptSystem.hpp"
#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace Axiom {
	void ImGuiEditorLayer::EnsureFBO(ViewportFBO& fbo, int width, int height) {
		if (width <= 0 || height <= 0) return;

		const bool sizeChanged = fbo.ViewportSize.GetWidth() != width || fbo.ViewportSize.GetHeight() != height;
		if (fbo.FramebufferId != 0 && !sizeChanged) return;

		DestroyFBO(fbo);
		fbo.ViewportSize.SetSize(width, height);

		glGenFramebuffers(1, &fbo.FramebufferId);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo.FramebufferId);

		glGenTextures(1, &fbo.ColorTextureId);
		glBindTexture(GL_TEXTURE_2D, fbo.ColorTextureId);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo.ColorTextureId, 0);

		glGenRenderbuffers(1, &fbo.DepthRenderbufferId);
		glBindRenderbuffer(GL_RENDERBUFFER, fbo.DepthRenderbufferId);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fbo.DepthRenderbufferId);

		AIM_ASSERT(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, AxiomErrorCode::InvalidHandle,
			"Viewport framebuffer is incomplete");

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void ImGuiEditorLayer::DestroyFBO(ViewportFBO& fbo) {
		if (fbo.DepthRenderbufferId != 0) {
			glDeleteRenderbuffers(1, &fbo.DepthRenderbufferId);
			fbo.DepthRenderbufferId = 0;
		}
		if (fbo.ColorTextureId != 0) {
			glDeleteTextures(1, &fbo.ColorTextureId);
			fbo.ColorTextureId = 0;
		}
		if (fbo.FramebufferId != 0) {
			glDeleteFramebuffers(1, &fbo.FramebufferId);
			fbo.FramebufferId = 0;
		}
	}

	void ImGuiEditorLayer::EnsureViewportFramebuffer(int width, int height) {
		EnsureFBO(m_EditorViewFBO, width, height);
	}

	void ImGuiEditorLayer::DestroyViewportFramebuffer() {
		DestroyFBO(m_EditorViewFBO);
	}

	void ImGuiEditorLayer::OnPreRender(Application& app) {
		(void)app;
		Scene* activeScene = SceneManager::Get().GetActiveScene();
		if (!activeScene) {
			return;
		}
		Scene& scene = *activeScene;

		// Deferred build execution: state 1 -> 2 lets the overlay render one frame first
		if (m_BuildState == 1) {
			m_BuildState = 2;
		} else if (m_BuildState == 2) {
			ExecuteBuild();
			m_BuildState = 0;

			// Launch the built game if "Build and Play" was clicked
			if (m_BuildAndPlay) {
				m_BuildAndPlay = false;
				AxiomProject* project = ProjectManager::GetCurrentProject();
				if (project) {
					std::string outputDir = m_BuildOutputDirBuffer;
					if (outputDir.empty())
						outputDir = Path::Combine(project->RootDirectory, "Builds");
#ifdef AIM_PLATFORM_WINDOWS
					auto exePath = std::filesystem::path(outputDir) / (project->Name + ".exe");
					if (std::filesystem::exists(exePath)) {
						std::string cmd = "\"" + std::filesystem::canonical(exePath).string() + "\"";
						// Proper UTF-8 → wide conversion. The previous byte-wise iterator
						// construction corrupted any path containing non-ASCII bytes
						// (Unicode user folders, accented project names), causing
						// CreateProcessW to fail silently on those systems.
						std::wstring wcmd;
						{
							const int needed = MultiByteToWideChar(CP_UTF8, 0,
								cmd.c_str(), static_cast<int>(cmd.size()), nullptr, 0);
							if (needed > 0) {
								wcmd.resize(static_cast<size_t>(needed));
								MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(),
									static_cast<int>(cmd.size()), wcmd.data(), needed);
							}
						}
						std::vector<wchar_t> buf(wcmd.begin(), wcmd.end());
						buf.push_back(L'\0');
						STARTUPINFOW si{}; si.cb = sizeof(si);
						PROCESS_INFORMATION pi{};
						if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr,
							FALSE, CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi)) {
							const DWORD errorCode = GetLastError();
							AIM_ERROR_TAG("Editor",
								"Failed to launch built game '{}': error {}",
								exePath.string(), errorCode);
						}
						else {
							CloseHandle(pi.hProcess);
							CloseHandle(pi.hThread);
							AIM_INFO_TAG("Build", "Launched: {}", exePath.string());
						}
					} else {
						AIM_CORE_ERROR_TAG("Build", "Built executable not found: {}", exePath.string());
					}
#endif
				}
			}
		}

		// Process OS file drops — forward to AssetBrowser
		{
			auto* app = Application::GetInstance();
			if (app) {
				auto drops = app->TakePendingFileDrops();
				if (!drops.empty() && m_AssetBrowserInitialized) {
					m_AssetBrowser.OnExternalFileDrop(drops);
				}
			}
		}

		// Process deferred scene drop from Hierarchy drag-and-drop (blocked during Play Mode)
		if (!m_PendingSceneFileDrop.empty() && !Application::GetIsPlaying()) {
			std::string dropPath = m_PendingSceneFileDrop;
			m_PendingSceneFileDrop.clear();

			std::string sceneName = std::filesystem::path(dropPath).stem().string();
			auto& sm = SceneManager::Get();
			if (!sm.HasSceneDefinition(sceneName))
				sm.RegisterScene(sceneName);
			if (!sm.IsSceneLoaded(sceneName)) {
				auto weakScene = sm.LoadSceneAdditive(sceneName);
				if (auto loaded = weakScene.lock()) {
					SceneSerializer::LoadFromFile(*loaded, dropPath);
					m_EntityOrder.clear();
				}
			}
		}

		// Process deferred scene switch (blocked during Play Mode)
		if (!m_PendingSceneSwitch.empty() && !Application::GetIsPlaying()) {
			std::string switchPath = m_PendingSceneSwitch;
			m_PendingSceneSwitch.clear();

			Scene* active = SceneManager::Get().GetActiveScene();
			if (active) {
				SceneSerializer::LoadFromFile(*active, switchPath);
				m_EntityOrder.clear();
				AxiomProject* project = ProjectManager::GetCurrentProject();
				if (project) {
					project->LastOpenedScene = std::filesystem::path(switchPath).stem().string();
					project->Save();
				}
			}
		}

		// Intercept Asset Browser scene load (blocked during Play Mode)
		if (!Application::GetIsPlaying()) {
			std::string pendingLoad = m_AssetBrowser.TakePendingSceneLoad();
			if (!pendingLoad.empty()) {
				Scene* active = SceneManager::Get().GetActiveScene();
				if (active && active->IsDirty()) {
					m_ConfirmDialogPendingPath = pendingLoad;
					m_ShowSaveConfirmDialog = true;
				} else {
					m_PendingSceneSwitch = pendingLoad;
				}
			}
		}

		// Intercept Asset Browser prefab double-click → enter prefab-edit
		// mode. Blocked during Play Mode (same as scene load) — editing
		// a prefab while a game is running would corrupt running scripts'
		// view of selection.
		if (!Application::GetIsPlaying()) {
			std::string pendingPrefabEdit = m_AssetBrowser.TakePendingPrefabEdit();
			if (!pendingPrefabEdit.empty()) {
				OpenPrefabForEditing(pendingPrefabEdit);
			}
		}

		// Ctrl+S: routes to prefab inspector if open, else saves active scene. Blocked in playmode.
		// Pass `repeat=false` so holding the key down doesn't re-fire Save every frame
		// (which would hammer disk + scene serialization at the OS key-repeat rate).
		if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && !Application::GetIsPlaying()) {
			if (m_PrefabInspector.IsOpen() && m_PrefabInspector.HasUnsavedChanges()) {
				m_PrefabInspector.Save();
			}
			else {
				Scene* active = SceneManager::Get().GetActiveScene();
				AxiomProject* project = ProjectManager::GetCurrentProject();
				if (active && project) {
					std::string scenePath = project->GetSceneFilePath(active->GetName());
					SceneSerializer::SaveToFile(*active, scenePath);
					project->LastOpenedScene = active->GetName();
					project->Save();
				}
			}
		}

		// Intercept quit request: exit playmode first, then check for unsaved changes
		if (Application::IsQuitRequested()) {
			// Exit playmode and restore scene first
			if (Application::GetIsPlaying()) {
				RestoreEditorSceneAfterPlaymode();
			}

			Scene* active = SceneManager::Get().GetActiveScene();
			if (active && active->IsDirty()) {
				m_ShowQuitSaveDialog = true;
				Application::CancelQuit();
			}
		}

		RenderDockspaceRoot();
		RenderMainMenu(scene);

		// Save confirmation modal dialog (scene switch)
		if (m_ShowSaveConfirmDialog) {
			ImGui::OpenPopup("Save Changes?");
			m_ShowSaveConfirmDialog = false;
		}
		if (ImGui::BeginPopupModal("Save Changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			Scene* active = SceneManager::Get().GetActiveScene();
			std::string activeName = active ? active->GetName() : "Scene";
			ImGui::Text("Save changes to %s before opening?", activeName.c_str());
			ImGui::Spacing();

			if (ImGui::Button("Save", ImVec2(100, 0))) {
				if (active) {
					AxiomProject* project = ProjectManager::GetCurrentProject();
					if (project) {
						std::string savePath = project->GetSceneFilePath(active->GetName());
						SceneSerializer::SaveToFile(*active, savePath);
					}
				}
				m_PendingSceneSwitch = m_ConfirmDialogPendingPath;
				m_ConfirmDialogPendingPath.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Don't Save", ImVec2(100, 0))) {
				m_PendingSceneSwitch = m_ConfirmDialogPendingPath;
				m_ConfirmDialogPendingPath.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(100, 0))) {
				m_ConfirmDialogPendingPath.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// Prefab save/discard modal — fired when the user picks a different
		// asset while a dirty prefab is open in the inspector.
		if (m_ShowPrefabSavePrompt) {
			ImGui::OpenPopup("Save Prefab Changes?");
			m_ShowPrefabSavePrompt = false;
		}
		if (ImGui::BeginPopupModal("Save Prefab Changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			std::string prefabName = std::filesystem::path(m_PrefabInspector.GetCurrentPath()).filename().string();
			if (prefabName.empty()) prefabName = "the prefab";
			ImGui::Text("Save changes to %s before switching?", prefabName.c_str());
			ImGui::Spacing();

			if (ImGui::Button("Save", ImVec2(100, 0))) {
				m_PrefabInspector.Save();
				if (!m_PendingPrefabSwitchPath.empty()) {
					m_PrefabInspector.Open(m_PendingPrefabSwitchPath);
					m_PrefabInspectorPath = m_PendingPrefabSwitchPath;
					m_PendingPrefabSwitchPath.clear();
				}
				else {
					m_PrefabInspector.Close();
					m_PrefabInspectorPath.clear();
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Don't Save", ImVec2(100, 0))) {
				if (!m_PendingPrefabSwitchPath.empty()) {
					m_PrefabInspector.Open(m_PendingPrefabSwitchPath);
					m_PrefabInspectorPath = m_PendingPrefabSwitchPath;
					m_PendingPrefabSwitchPath.clear();
				}
				else {
					m_PrefabInspector.Close();
					m_PrefabInspectorPath.clear();
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(100, 0))) {
				m_PendingPrefabSwitchPath.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// Quit confirmation modal dialog
		if (m_ShowQuitSaveDialog) {
			ImGui::OpenPopup("Save Before Quit?");
			m_ShowQuitSaveDialog = false;
		}
		if (ImGui::BeginPopupModal("Save Before Quit?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			Scene* active = SceneManager::Get().GetActiveScene();
			std::string activeName = active ? active->GetName() : "Scene";
			ImGui::Text("Save changes to %s before closing?", activeName.c_str());
			ImGui::Spacing();

			if (ImGui::Button("Save", ImVec2(100, 0))) {
				if (active) {
					AxiomProject* project = ProjectManager::GetCurrentProject();
					if (project) {
						std::string savePath = project->GetSceneFilePath(active->GetName());
						SceneSerializer::SaveToFile(*active, savePath);
						project->LastOpenedScene = active->GetName();
						project->Save();
					}
				}
				ImGui::CloseCurrentPopup();
				Application::ConfirmQuit();
			}
			ImGui::SameLine();
			if (ImGui::Button("Don't Save", ImVec2(100, 0))) {
				ImGui::CloseCurrentPopup();
				Application::ConfirmQuit();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(100, 0))) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// Suppress script recompilation while a script is being created/renamed
		if (scene.HasSystem<ScriptSystem>()) {
			auto* scriptSys = scene.GetSystem<ScriptSystem>();
				if (scriptSys)
					scriptSys->SetRecompileSuppressed(m_AssetBrowser.IsCreatingScript());
		}

		PollPendingPlayModeRequest(scene);

		RenderToolbar();
		RenderEntitiesPanel();
		// Inspector + editor view follow the prefab-edit override when
		// active so component edits and viewport rendering both target
		// the detached prefab scene. Game View intentionally stays on
		// the real active scene so the user can still preview play.
		Scene* contextScene = GetContextScene();
		RenderInspectorPanel(contextScene ? *contextScene : scene);
		RenderEditorView(contextScene ? *contextScene : scene);
		RenderGameView(scene);
		RenderProjectPanel();
		RenderLogPanel();
		RenderBuildPanel();
		RenderPlayerSettingsPanel();
		RenderPackageManagerPanel();

		// Profiler panel — Ctrl+F6 toggle. Panel manages its own visibility-gating internally.
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F6)) {
			m_ShowProfiler = !m_ShowProfiler;
		}
		if (m_ShowProfiler) {
			m_ProfilerPanel.Render(&m_ShowProfiler);
		}

		// Build progress overlay (same pattern as ScriptSystem compilation overlay)
		if (m_BuildState > 0) {
			ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImVec2 center(viewport->Pos.x + viewport->Size.x * 0.5f,
				viewport->Pos.y + viewport->Size.y * 0.5f);

			ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2(320, 80));
			ImGui::SetNextWindowBgAlpha(0.92f);

			ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
				| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
				| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;

			ImGui::Begin("##BuildOverlay", nullptr, overlayFlags);
			ImGui::TextUnformatted("Building Project...");
			ImGui::Spacing();
			float elapsed = std::chrono::duration<float>(
				std::chrono::steady_clock::now() - m_BuildStartTime).count();
			ImGui::ProgressBar(fmodf(elapsed * 0.4f, 1.0f), ImVec2(-1, 0), "");
			ImGui::End();
		}

		// Script rebuild overlay — rendered editor-side so the engine has no ImGui dep.
		if (scene.HasSystem<ScriptSystem>()) {
			auto* scriptSys = scene.GetSystem<ScriptSystem>();
			if (scriptSys && (scriptSys->IsScriptRebuildRunning() || scriptSys->IsNativeRebuildRunning())) {
				ImGuiViewport* viewport = ImGui::GetMainViewport();
				ImVec2 center(viewport->Pos.x + viewport->Size.x * 0.5f,
					viewport->Pos.y + viewport->Size.y * 0.5f);

				ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
				ImGui::SetNextWindowSize(ImVec2(320, 80));
				ImGui::SetNextWindowBgAlpha(0.92f);

				ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
					| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
					| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav;

				ImGui::Begin("##ScriptBuildOverlay", nullptr, overlayFlags);
				ImGui::TextUnformatted(scriptSys->IsNativeRebuildRunning() ? "Compiling Native Scripts..." : "Compiling Scripts...");
				ImGui::Spacing();
				const float elapsed = scriptSys->GetActiveRebuildElapsedSeconds();
				ImGui::ProgressBar(fmodf(elapsed * 0.4f, 1.0f), ImVec2(-1, 0), "");
				ImGui::End();
			}
		}

		ImGui::End(); // Close dockspace opened in RenderDockspaceRoot.
	}
}
