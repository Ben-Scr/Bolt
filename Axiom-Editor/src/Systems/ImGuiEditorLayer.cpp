#include <pch.hpp>
#include "ImGuiEditorLayer.hpp"

#include <imgui.h>
#include <imgui_internal.h>
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
#include "Serialization/Json.hpp"
#include "Serialization/File.hpp"
#include "Utils/Process.hpp"
#include "Packages/NuGetSource.hpp"
#include "Packages/GitHubSource.hpp"
#include "Editor/ApplicationEditorAccess.hpp"
#include "Editor/EditorComponentRegistration.hpp"
#include "Editor/ExternalEditor.hpp"
#include "Inspector/ReferencePicker.hpp"
#include "Gui/EditorIcons.hpp"
#include "Gui/EditorTheme.hpp"
#include "Gui/HierarchyDragData.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Scripting/ScriptDiscovery.hpp"
#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace Axiom {
	namespace {
		void StartPlayOnAwakeComponents(Scene& scene)
		{
			auto audioView = scene.GetRegistry().view<AudioSourceComponent>(entt::exclude<DisabledTag>);
			for (auto [ent, audio] : audioView.each()) {
				if (audio.GetPlayOnAwake() && audio.GetAudioHandle().IsValid()) {
					audio.Play();
				}
			}

			auto particleView = scene.GetRegistry().view<ParticleSystem2DComponent>(entt::exclude<DisabledTag>);
			for (auto [ent, particleSystem] : particleView.each()) {
				if (particleSystem.PlayOnAwake) {
					particleSystem.Play();
				}
			}
		}

		std::string BuildScriptMenuLabel(const EditorScriptDiscovery::ScriptEntry& scriptEntry)
		{
			return scriptEntry.ClassName + "  " + scriptEntry.Extension;
		}

		bool AttachScriptToEntity(Entity entity, Scene& scene, const EditorScriptDiscovery::ScriptEntry& scriptEntry)
		{
			if (scriptEntry.ClassName.empty() || scriptEntry.Type == ScriptType::Unknown) {
				return false;
			}

			if (!entity.HasComponent<ScriptComponent>()) {
				entity.AddComponent<ScriptComponent>();
			}

			auto& scriptComponent = entity.GetComponent<ScriptComponent>();
			if (scriptComponent.HasScript(scriptEntry.ClassName, scriptEntry.Type)) {
				return false;
			}

			scriptComponent.AddScript(scriptEntry.ClassName, scriptEntry.Type);
			scene.MarkDirty();
			return true;
		}

		bool AttachManagedComponentToEntity(Entity entity, Scene& scene, const EditorScriptDiscovery::ScriptEntry& scriptEntry)
		{
			if (scriptEntry.ClassName.empty() || !scriptEntry.IsManagedComponent) {
				return false;
			}

			if (!entity.HasComponent<ScriptComponent>()) {
				entity.AddComponent<ScriptComponent>();
			}

			auto& scriptComponent = entity.GetComponent<ScriptComponent>();
			if (scriptComponent.HasManagedComponent(scriptEntry.ClassName)) {
				return false;
			}

			scriptComponent.AddManagedComponent(scriptEntry.ClassName);
			scene.MarkDirty();
			return true;
		}

		bool IsLeftMouseDragPastClickThreshold()
		{
			const ImGuiIO& io = ImGui::GetIO();
			const ImVec2 delta(io.MousePos.x - io.MouseClickedPos[ImGuiMouseButton_Left].x,
				io.MousePos.y - io.MouseClickedPos[ImGuiMouseButton_Left].y);
			return (delta.x * delta.x + delta.y * delta.y) > (io.MouseDragThreshold * io.MouseDragThreshold);
		}

		struct ComponentClipboardPayload {
			std::string SerializedName;
			Json::Value Data;
		};

		bool TryReadComponentClipboard(const std::string& clipboardJson, ComponentClipboardPayload& outPayload)
		{
			if (clipboardJson.empty()) {
				return false;
			}

			Json::Value root;
			std::string parseError;
			if (!Json::TryParse(clipboardJson, root, &parseError) || !root.IsObject()) {
				return false;
			}

			const Json::Value* componentValue = root.FindMember("component");
			const Json::Value* dataValue = root.FindMember("data");
			if (!componentValue || !componentValue->IsString() || !dataValue || dataValue->IsNull()) {
				return false;
			}

			outPayload.SerializedName = componentValue->AsStringOr();
			outPayload.Data = *dataValue;
			return !outPayload.SerializedName.empty();
		}

		const ComponentInfo* FindComponentInfoBySerializedName(const ComponentRegistry& registry, const std::string& serializedName)
		{
			const ComponentInfo* found = nullptr;
			registry.ForEachComponentInfo([&](const std::type_index&, const ComponentInfo& info) {
				if (!found && info.category == ComponentCategory::Component && info.serializedName == serializedName) {
					found = &info;
				}
			});
			return found;
		}
	}



	// ──────────────────────────────────────────────
	//  Lifecycle
	// ──────────────────────────────────────────────

	void ImGuiEditorLayer::OnAttach(Application& app) {
		Application::SetIsPlaying(false);
		ApplicationEditorAccess::SetGameInputEnabled(false);
		Gizmo::SetShowInRuntime(false);
		if (app.GetRenderer2D()) {
			app.GetRenderer2D()->SetSkipBeginFrameRender(true);
		}

		if (m_LogSubscriptionId.value != 0) {
			Log::OnLog.Remove(m_LogSubscriptionId);
		}

		// Profiler panel — initialize once when the editor layer attaches.
		// Lazy first-render still loads project settings, so doing this
		// before a project is loaded is fine.
		m_ProfilerPanel.Initialize();

		ClearLogEntries();
		m_LogDispatchState = std::make_shared<LogDispatchState>();
		std::weak_ptr<LogDispatchState> weakLogDispatchState = m_LogDispatchState;
		m_LogSubscriptionId = Log::OnLog.Add([weakLogDispatchState](const Log::Entry& entry) {
			// Only show user-facing logs in editor panel (Client + EditorConsole)
			// Core/engine logs still go to stdout but not the editor UI
			if (entry.Source == Log::Type::Core) return;

			if (std::shared_ptr<LogDispatchState> state = weakLogDispatchState.lock()) {
				std::scoped_lock lock(state->Mutex);
				state->PendingEntries.push_back({ entry.Message, entry.Level });
			}
			});
	}

	void ImGuiEditorLayer::OnDetach(Application& app) {
		(void)app;
		m_ProfilerPanel.Shutdown();
		ApplicationEditorAccess::SetGameInputEnabled(true);
		Gizmo::SetShowInRuntime(true);
		if (m_LogSubscriptionId.value != 0) {
			Log::OnLog.Remove(m_LogSubscriptionId);
			m_LogSubscriptionId = EventId{};
		}
		m_LogDispatchState.reset();
		ClearPreviewTextureCache();

		// Reverse construction order: panels → icons → FBOs.
		m_AssetBrowser.Shutdown();
		m_PackageManagerPanel.Shutdown();
		m_PackageManager.Shutdown();
		EditorIcons::Shutdown();
		DestroyFBO(m_EditorViewFBO);
		DestroyFBO(m_GameViewFBO);
	}

	void ImGuiEditorLayer::OnUpdate(Application& app, float dt) {
		(void)dt;
		DrainPendingLogEntries();

		Input& input = app.GetInput();
		Scene* activeScene = app.GetSceneManager() ? app.GetSceneManager()->GetActiveScene() : nullptr;
		if (!activeScene) {
			return;
		}

		Scene& scene = *activeScene;
		const bool hasShortcutFocus = HasEntityShortcutFocus();
		const bool hasEntitySelection = !GetSelectedEntities(scene).empty();

		if (input.GetKeyDown(KeyCode::F) && hasShortcutFocus && hasEntitySelection) {
			FocusSelectedEntity(scene);
		}

		if (Application::GetIsPlaying() || !hasShortcutFocus) {
			return;
		}

		if (m_RenamingEntity != entt::null || ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive()) {
			return;
		}

		// Use ImGui's KeyCtrl + IsKeyPressed instead of the raw Input wrapper —
		// the engine's KeyCode::LeftControl misses RIGHT Ctrl (and Cmd on macOS),
		// which is why the prior code silently failed for users on RHS keyboards.
		// IsKeyPressed(_, repeat=false) also prevents auto-repeat from firing
		// the action multiple times when the key is held. Same pattern as the
		// Save shortcut wired up in ImGuiEditorLayerChrome.cpp.
		const ImGuiIO& io = ImGui::GetIO();
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && hasEntitySelection) {
			CopySelectedEntities(scene);
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
			PasteEntities(scene);
		}
		if (!hasEntitySelection) {
			return;
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
			DuplicateSelectedEntity(scene);
		}
		if (input.GetKeyDown(KeyCode::Delete) || input.GetKeyDown(KeyCode::KpDecimal)) {
			DeleteSelectedEntity(scene);
		}
		if (input.GetKeyDown(KeyCode::F2)) {
			BeginRenameSelectedEntity(scene);
		}
	}

	void ImGuiEditorLayer::DrainPendingLogEntries() {
		if (!m_LogDispatchState) {
			return;
		}

		std::vector<LogEntry> pendingEntries;
		{
			std::scoped_lock lock(m_LogDispatchState->Mutex);
			if (m_LogDispatchState->PendingEntries.empty()) {
				return;
			}

			pendingEntries.swap(m_LogDispatchState->PendingEntries);
		}

		for (LogEntry& entry : pendingEntries) {
			AppendLogEntry(std::move(entry));
		}
	}

	void ImGuiEditorLayer::AppendLogEntry(LogEntry entry) {
		m_LogEntries.push_back(std::move(entry));
		if (m_LogEntries.size() > 2000) {
			m_LogEntries.erase(m_LogEntries.begin(), m_LogEntries.begin() + 500);
		}
	}

	void ImGuiEditorLayer::ClearLogEntries() {
		m_LogEntries.clear();
		if (m_LogDispatchState) {
			std::scoped_lock lock(m_LogDispatchState->Mutex);
			m_LogDispatchState->PendingEntries.clear();
		}
	}

	// ──────────────────────────────────────────────
	//  Dockspace & Menu
	// ──────────────────────────────────────────────

	void ImGuiEditorLayer::RenderDockspaceRoot() {
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::SetNextWindowViewport(viewport->ID);

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

		ImGui::Begin("EditorDockspace", nullptr, flags);
		ImGui::PopStyleVar(3);

		ImGuiID dockspaceId = ImGui::GetID("AxiomEditorDockspace");
		ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
		// End() is in OnPreRender after RenderMainMenu — must stay open for the menubar.
	}

	void ImGuiEditorLayer::RenderMainMenu(Scene& scene) {
		if (!ImGui::BeginMenuBar()) {
			return;
		}


		if (ImGui::BeginMenu("Application")) {
			if (ImGui::MenuItem("Reload App")) {
				Application::Reload();
			}
			if (ImGui::MenuItem("Quit")) {
				Application::RequestQuit();
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, !Application::GetIsPlaying())) {
				AxiomProject* project = ProjectManager::GetCurrentProject();
				if (project) {
					std::string scenePath = project->GetSceneFilePath(scene.GetName());
					SceneSerializer::SaveToFile(scene, scenePath);
					project->LastOpenedScene = scene.GetName();
					project->Save();
				}
			}
			ImGui::Separator();

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Project")) {
			bool hasProject = ProjectManager::HasProject();
			if (ImGui::MenuItem("Build", nullptr, false, hasProject && !Application::GetIsPlaying())) {
				m_ShowBuildPanel = true;
				if (m_BuildOutputDir.empty()) {
					AxiomProject* project = ProjectManager::GetCurrentProject();
					if (project) {
						m_BuildOutputDir = Path::Combine(project->RootDirectory, "Builds", "Windows");
						std::snprintf(m_BuildOutputDirBuffer, sizeof(m_BuildOutputDirBuffer), "%s", m_BuildOutputDir.c_str());
					}
				}
			}
			if (ImGui::MenuItem("Project Settings", nullptr, false, hasProject)) {
				m_ShowPlayerSettings = true;
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Package Manager", nullptr, false, hasProject)) {
				m_ShowPackageManager = true;
			}

			ExternalEditor::DetectEditors();
			const auto& editors = ExternalEditor::GetAvailableEditors();
			if (!editors.empty() && ImGui::BeginMenu("Script Editor")) {
				for (int i = 0; i < static_cast<int>(editors.size()); i++) {
					bool selected = (ExternalEditor::GetSelectedIndex() == i);
					const std::string editorId = std::to_string(i);
					if (ImGuiUtils::MenuItemEllipsis(editors[i].DisplayName, editorId.c_str(), nullptr, selected, true, 220.0f)) {
						ExternalEditor::SetSelectedIndex(i);
					}
				}
				ImGui::EndMenu();
			}

			ImGui::EndMenu();
		}

		// Tools — runtime/diagnostic utilities. Profiler panel is the
		// first inhabitant; later additions (memory tracker, log filter,
		// etc.) belong here too.
		if (ImGui::BeginMenu("Tools")) {
			ImGui::MenuItem("Profiler", "Ctrl+F6", &m_ShowProfiler);
			ImGui::EndMenu();
		}

		ImGui::EndMenuBar();
	}

	static bool IconButton(const char* id, const char* iconName, float iconSize, const ImVec2& btnSize) {
		unsigned int tex = EditorIcons::Get(iconName, (int)iconSize);
		if (tex)
			return ImGui::ImageButton(id, (ImTextureID)(intptr_t)tex, btnSize, ImVec2(0, 1), ImVec2(1, 0));
		return ImGui::Button(id + 2);
	}

	void ImGuiEditorLayer::BeginPlayModeRequest(Scene& scene) {
		AxiomProject* project = ProjectManager::GetCurrentProject();
		if (project) {
			std::string scenePath = project->GetSceneFilePath(scene.GetName());
			if (scene.IsDirty()) {
				SceneSerializer::SaveToFile(scene, scenePath);
				project->LastOpenedScene = scene.GetName();
				project->Save();
			}
			m_PlayModeScenePath = scenePath;
		}

		m_LogEntries.clear();
		ApplicationEditorAccess::SetPlaymodePaused(false);

		ScriptSystem* scriptSys = scene.HasSystem<ScriptSystem>() ? scene.GetSystem<ScriptSystem>() : nullptr;
		if (scriptSys && scriptSys->IsRebuilding()) {
			m_PlayModeRecompilePending = true;
			return;
		}
		if (scriptSys && !scriptSys->DidLastRebuildSucceed()) {
			AIM_ERROR_TAG("PlayMode", "Script compilation failed. Play Mode was not started.");
			return;
		}

		CompletePlayModeEntry(scene);
	}

	void ImGuiEditorLayer::CompletePlayModeEntry(Scene& scene) {
		m_PlayModeRecompilePending = false;
		ApplicationEditorAccess::SetPlaymodePaused(false);
		Application::SetIsPlaying(true);
		// Reset Box2D sleep timers so bodies that sat through editor
		// idle don't immediately freeze on the first physics step.
		if (PhysicsSystem2D::IsInitialized()) {
			PhysicsSystem2D::WakeAllBodies();
		}
		StartPlayOnAwakeComponents(scene);
	}

	void ImGuiEditorLayer::PollPendingPlayModeRequest(Scene& scene) {
		if (!m_PlayModeRecompilePending || Application::GetIsPlaying()) {
			return;
		}

		ScriptSystem* scriptSys = scene.HasSystem<ScriptSystem>() ? scene.GetSystem<ScriptSystem>() : nullptr;
		if (!scriptSys || !scriptSys->IsRebuilding()) {
			if (!scriptSys || scriptSys->DidLastRebuildSucceed()) {
				CompletePlayModeEntry(scene);
			}
			else {
				m_PlayModeRecompilePending = false;
				AIM_ERROR_TAG("PlayMode", "Script compilation failed. Play Mode was not started.");
			}
		}
	}

	void ImGuiEditorLayer::RenderToolbar() {
		ImGui::Begin("Toolbar");

		const float iconSize = ImGui::GetTextLineHeight();
		const ImVec2 btnSize(iconSize, iconSize);
		const bool isPlaying = Application::GetIsPlaying();
		const bool isPaused = ApplicationEditorAccess::IsPlaymodePaused();

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));

		if (!isPlaying) {
			const bool playButtonDisabled = m_PlayModeRecompilePending;
			if (playButtonDisabled) {
				ImGui::BeginDisabled();
			}
			if (IconButton("##Play", "play", iconSize, btnSize)) {
				Scene* active = SceneManager::Get().GetActiveScene();
				if (active) {
					BeginPlayModeRequest(*active);
				}
			}
			if (playButtonDisabled) {
				ImGui::EndDisabled();
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play (Enter playmode)");
		}
		else {
			if (isPaused) {
				if (IconButton("##Continue", "play", iconSize, btnSize)) {
					ApplicationEditorAccess::SetPlaymodePaused(false);
					Scene* active = SceneManager::Get().GetActiveScene();
					if (active) {
						for (auto ent : m_EditorPausedAudioEntities) {
							if (active->IsValid(ent) && active->HasComponent<AudioSourceComponent>(ent)) {
								auto& audio = active->GetComponent<AudioSourceComponent>(ent);
								if (audio.IsPaused()) audio.Resume();
							}
						}
					}
					m_EditorPausedAudioEntities.clear();
				}
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Continue");
			}
			else {
				if (IconButton("##Pause", "pause", iconSize, btnSize)) {
					ApplicationEditorAccess::SetPlaymodePaused(true);
					m_EditorPausedAudioEntities.clear();
					Scene* active = SceneManager::Get().GetActiveScene();
					if (active) {
						for (auto [ent, audio] : active->GetRegistry().view<AudioSourceComponent>().each()) {
							if (audio.IsPlaying()) {
								audio.Pause();
								m_EditorPausedAudioEntities.push_back(ent);
							}
						}
					}
				}
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pause");
			}

			ImGui::SameLine();
			if (IconButton("##Step", "step_forward", iconSize, btnSize)) {
				ApplicationEditorAccess::SetPlaymodePaused(false);
				m_StepFrames = 2;
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step (advance one frame)");

			ImGui::SameLine();
			if (IconButton("##Stop", "stop", iconSize, btnSize)) {
				{
					Scene* act = SceneManager::Get().GetActiveScene();
					if (act) {
						for (auto [ent, audio] : act->GetRegistry().view<AudioSourceComponent>().each()) {
							if (audio.IsPlaying() || audio.IsPaused()) audio.Stop();
						}
					}
				}

				RestoreEditorSceneAfterPlaymode();
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop (exit playmode)");
		}

		ImGui::PopStyleVar();
		ImGui::SameLine();
		const char* statusText = !isPlaying ? "Editor" : (isPaused ? "Paused" : "Playing");
		ImGui::TextUnformatted(statusText);
		if (m_PlayModeRecompilePending) {
			ImGui::SameLine();
			ImGui::TextDisabled("Waiting for script compilation...");
		}
		ImGui::End();

		// Handle step-frame logic: pause again after stepping one frame
		if (isPlaying && m_StepFrames > 0) {
			m_StepFrames--;
			if (m_StepFrames == 0) {
				ApplicationEditorAccess::SetPlaymodePaused(true);
				// SetPlaymodePaused only suspends gameplay update; audio runs on
				// miniaudio's own thread, so we mirror the Pause button's manual
				// capture. List is appended (not cleared) so any entries from a
				// prior explicit Pause survive to be resumed by Continue.
				Scene* active = SceneManager::Get().GetActiveScene();
				if (active) {
					for (auto [ent, audio] : active->GetRegistry().view<AudioSourceComponent>().each()) {
						if (audio.IsPlaying()) {
							audio.Pause();
							m_EditorPausedAudioEntities.push_back(ent);
						}
					}
				}
			}
		}
	}

	void ImGuiEditorLayer::RestoreEditorSceneAfterPlaymode() {
		uint64_t selectedUUID = 0;
		Scene* active = SceneManager::Get().GetActiveScene();
		if (active && m_SelectedEntity != entt::null && active->IsValid(m_SelectedEntity)
			&& active->HasComponent<UUIDComponent>(m_SelectedEntity)) {
			selectedUUID = static_cast<uint64_t>(active->GetComponent<UUIDComponent>(m_SelectedEntity).Id);
		}

		m_SelectedEntity = entt::null;
		m_IsSceneNodeSelected = false;
		m_RenamingEntity = entt::null;
		m_EntityOrder.clear();
		m_PlayModeRecompilePending = false;

		ApplicationEditorAccess::SetPlaymodePaused(false);
		Application::SetIsPlaying(false);

		if (!m_PlayModeScenePath.empty()) {
			active = SceneManager::Get().GetActiveScene();
			if (active) {
				SceneSerializer::LoadFromFile(*active, m_PlayModeScenePath);
			}
			m_PlayModeScenePath.clear();
		}

		if (selectedUUID == 0) {
			return;
		}

		active = SceneManager::Get().GetActiveScene();
		if (!active) {
			return;
		}

		auto view = active->GetRegistry().view<UUIDComponent>();
		for (auto [ent, uuid] : view.each()) {
			if (static_cast<uint64_t>(uuid.Id) == selectedUUID) {
				SelectEntity(ent);
				break;
			}
		}
	}

	void ImGuiEditorLayer::SelectSceneNode() {
		m_SelectedEntity = entt::null;
		m_PressedEntity = entt::null;
		m_SelectedEntities.clear();
		m_LastEntitySelectionIndex = -1;
		m_RenamingEntity = entt::null;
		m_EntityRenameFrameCounter = 0;
		m_IsSceneNodeSelected = true;
		m_AssetBrowser.ClearSelection();
	}

	void ImGuiEditorLayer::SelectEntity(EntityHandle entity) {
		m_SelectedEntity = entity;
		m_SelectedEntities.clear();
		if (entity != entt::null) {
			m_SelectedEntities.push_back(entity);
		}
		m_LastEntitySelectionIndex = -1;
		m_IsSceneNodeSelected = false;
		if (entity != entt::null) {
			m_AssetBrowser.ClearSelection();
		}
	}

	void ImGuiEditorLayer::ClearEntitySelection() {
		m_SelectedEntity = entt::null;
		m_PressedEntity = entt::null;
		m_SelectedEntities.clear();
		m_LastEntitySelectionIndex = -1;
		m_RenamingEntity = entt::null;
		m_EntityRenameFrameCounter = 0;
		m_IsSceneNodeSelected = false;
	}

	bool ImGuiEditorLayer::IsEntitySelected(EntityHandle entity) const {
		return std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), entity) != m_SelectedEntities.end();
	}

	std::vector<EntityHandle> ImGuiEditorLayer::GetSelectedEntities(const Scene& scene) const {
		std::vector<EntityHandle> entities;
		for (EntityHandle entity : m_SelectedEntities) {
			if (entity != entt::null && scene.IsValid(entity)
				&& std::find(entities.begin(), entities.end(), entity) == entities.end()) {
				entities.push_back(entity);
			}
		}

		if (entities.empty() && m_SelectedEntity != entt::null && scene.IsValid(m_SelectedEntity)) {
			entities.push_back(m_SelectedEntity);
		}

		return entities;
	}

	void ImGuiEditorLayer::SetSingleEntitySelection(EntityHandle entity, int index) {
		m_SelectedEntity = entity;
		m_SelectedEntities.clear();
		if (entity != entt::null) {
			m_SelectedEntities.push_back(entity);
			m_AssetBrowser.ClearSelection();
		}
		m_LastEntitySelectionIndex = index;
		m_IsSceneNodeSelected = false;
	}

	void ImGuiEditorLayer::ToggleEntitySelection(EntityHandle entity, int index) {
		auto it = std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), entity);
		if (it != m_SelectedEntities.end()) {
			m_SelectedEntities.erase(it);
			if (m_SelectedEntity == entity) {
				m_SelectedEntity = m_SelectedEntities.empty() ? entt::null : m_SelectedEntities.back();
			}
		}
		else if (entity != entt::null) {
			m_SelectedEntities.push_back(entity);
			m_SelectedEntity = entity;
			m_AssetBrowser.ClearSelection();
		}

		m_LastEntitySelectionIndex = index;
		m_IsSceneNodeSelected = false;
	}

	void ImGuiEditorLayer::SelectEntityRange(int index) {
		if (m_EntityOrder.empty()) {
			return;
		}

		if (index < 0 || index >= static_cast<int>(m_EntityOrder.size())) {
			return;
		}

		if (m_LastEntitySelectionIndex < 0 || m_LastEntitySelectionIndex >= static_cast<int>(m_EntityOrder.size())) {
			SetSingleEntitySelection(m_EntityOrder[static_cast<std::size_t>(index)], index);
			return;
		}

		const int first = std::min(m_LastEntitySelectionIndex, index);
		const int last = std::max(m_LastEntitySelectionIndex, index);
		m_SelectedEntities.clear();
		for (int i = first; i <= last; ++i) {
			m_SelectedEntities.push_back(m_EntityOrder[static_cast<std::size_t>(i)]);
		}
		m_SelectedEntity = m_EntityOrder[static_cast<std::size_t>(index)];
		m_IsSceneNodeSelected = false;
		m_AssetBrowser.ClearSelection();
	}

	void ImGuiEditorLayer::FocusSelectedEntity(Scene& scene) {
		if (m_SelectedEntity == entt::null || !scene.IsValid(m_SelectedEntity)) {
			return;
		}

		Transform2DComponent* transform = nullptr;
		if (scene.TryGetComponent<Transform2DComponent>(m_SelectedEntity, transform) && transform) {
			m_EditorCamera.SetPosition(transform->Position);
		}
	}

	void ImGuiEditorLayer::DuplicateSelectedEntity(Scene& scene) {
		const std::vector<EntityHandle> selectedEntities = GetSelectedEntities(scene);
		if (selectedEntities.empty()) {
			return;
		}

		std::vector<EntityHandle> createdEntities;
		for (EntityHandle sourceEntity : selectedEntities) {
			Json::Value entityValue = SceneSerializer::SerializeEntityForClipboard(scene, sourceEntity);
			if (!entityValue.IsObject()) {
				continue;
			}

			if (Json::Value* nameValue = entityValue.FindMember("name")) {
				const std::string sourceName = nameValue->AsStringOr("Entity");
				*nameValue = Json::Value(sourceName + " (Clone)");
			}

			EntityHandle clone = SceneSerializer::DeserializeEntityFromValue(scene, entityValue);
			if (clone != entt::null) {
				createdEntities.push_back(clone);
			}
		}

		if (!createdEntities.empty()) {
			m_SelectedEntities = createdEntities;
			m_SelectedEntity = createdEntities.back();
			m_LastEntitySelectionIndex = -1;
			m_IsSceneNodeSelected = false;
			scene.MarkDirty();
			m_EntityOrder.clear();
		}
	}

	void ImGuiEditorLayer::DeleteSelectedEntity(Scene& scene) {
		const std::vector<EntityHandle> selectedEntities = GetSelectedEntities(scene);
		if (selectedEntities.empty()) {
			return;
		}

		for (EntityHandle entity : selectedEntities) {
			if (scene.IsValid(entity)) {
				scene.DestroyEntity(entity);
			}
		}
		ClearEntitySelection();
		m_EntityOrder.clear();
	}

	void ImGuiEditorLayer::BeginRenameSelectedEntity(Scene& scene) {
		if (m_SelectedEntity == entt::null || !scene.IsValid(m_SelectedEntity)) {
			return;
		}

		Entity entity = scene.GetEntity(m_SelectedEntity);
		m_RenamingEntity = m_SelectedEntity;
		m_EntityRenameFrameCounter = 0;
		std::snprintf(m_EntityRenameBuffer, sizeof(m_EntityRenameBuffer), "%s", entity.GetName().c_str());
	}

	void ImGuiEditorLayer::UnpackSelectedPrefabs(Scene& scene) {
		const std::vector<EntityHandle> selectedEntities = GetSelectedEntities(scene);
		if (selectedEntities.empty()) {
			return;
		}

		// Reusing SetEntityMetaData(EntityOrigin::Scene) is the canonical
		// reverse of the create-prefab path: it clears PrefabGUID, drops the
		// PrefabInstanceComponent, allocates a fresh SceneGUID, and marks
		// the scene dirty when not playing — so unpack is the exact inverse
		// of "drag entity to Asset Browser".
		bool anyUnpacked = false;
		for (EntityHandle entity : selectedEntities) {
			if (!scene.IsValid(entity)) continue;
			if (!scene.HasComponent<PrefabInstanceComponent>(entity)) continue;
			scene.SetEntityMetaData(entity, EntityOrigin::Scene);
			anyUnpacked = true;
		}
		if (anyUnpacked) {
			scene.MarkDirty();
		}
	}

	void ImGuiEditorLayer::CopySelectedEntities(Scene& scene) {
		const std::vector<EntityHandle> selectedEntities = GetSelectedEntities(scene);
		if (selectedEntities.empty()) {
			return;
		}

		Json::Value root = Json::Value::MakeObject();
		root.AddMember("type", Json::Value("EntityClipboard"));

		Json::Value entities = Json::Value::MakeArray();
		for (EntityHandle entity : selectedEntities) {
			Json::Value entityValue = SceneSerializer::SerializeEntityForClipboard(scene, entity);
			if (entityValue.IsObject()) {
				entities.Append(std::move(entityValue));
			}
		}

		if (entities.GetArray().empty()) {
			return;
		}

		root.AddMember("entities", std::move(entities));
		m_EntityClipboardJson = Json::Stringify(root, false);
	}

	void ImGuiEditorLayer::PasteEntities(Scene& scene) {
		if (m_EntityClipboardJson.empty()) {
			return;
		}

		Json::Value root;
		std::string parseError;
		if (!Json::TryParse(m_EntityClipboardJson, root, &parseError) || !root.IsObject()) {
			return;
		}

		const Json::Value* typeValue = root.FindMember("type");
		const Json::Value* entitiesValue = root.FindMember("entities");
		if (!typeValue || typeValue->AsStringOr() != "EntityClipboard" || !entitiesValue || !entitiesValue->IsArray()) {
			return;
		}

		std::vector<EntityHandle> createdEntities;
		for (const Json::Value& entityValue : entitiesValue->GetArray()) {
			EntityHandle entity = SceneSerializer::DeserializeEntityFromValue(scene, entityValue);
			if (entity != entt::null) {
				createdEntities.push_back(entity);
			}
		}

		if (!createdEntities.empty()) {
			m_SelectedEntities = createdEntities;
			m_SelectedEntity = createdEntities.back();
			m_LastEntitySelectionIndex = -1;
			m_IsSceneNodeSelected = false;
			scene.MarkDirty();
			m_EntityOrder.clear();
		}
	}

	bool ImGuiEditorLayer::HasEntityShortcutFocus() const {
		return m_IsEditorViewFocused || m_IsEntitiesPanelFocused || m_IsInspectorPanelFocused;
	}



	// ──────────────────────────────────────────────
	//  Entities Panel (with rename support)
	// ──────────────────────────────────────────────

	void ImGuiEditorLayer::RenderEntitiesPanel() {
		ImGui::Begin("Entities");

		Scene* activeScene = SceneManager::Get().GetActiveScene();

		// Right-click on empty space: create entities in the active scene
		if (activeScene && ImGui::BeginPopupContextWindow("EntityCreateContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			Scene& scene = *activeScene;
			if (ImGui::MenuItem("Create Entity"))
			{
				Entity created = scene.CreateEntity("Entity " + std::to_string(EntityHelper::EntitiesCount()));
				SelectEntity(created.GetHandle());
			}

			if (ImGui::BeginMenu("2D Entity"))
			{
				if (ImGui::BeginMenu("Sprite"))
				{
					if (ImGui::MenuItem("Square"))
					{
						Entity created = scene.CreateEntity("Square " + std::to_string(EntityHelper::EntitiesCount()));
						created.AddComponent<SpriteRendererComponent>();
						SelectEntity(created.GetHandle());
					}
					if (ImGui::MenuItem("Circle"))
					{
						Entity created = scene.CreateEntity("Circle " + std::to_string(EntityHelper::EntitiesCount()));
						auto& sprite = created.AddComponent<SpriteRendererComponent>();
						sprite.TextureHandle = TextureManager::GetDefaultTexture(DefaultTexture::Circle);
						SelectEntity(created.GetHandle());
					}
					if (ImGui::MenuItem("9Sliced"))
					{
						Entity created = scene.CreateEntity("9Sliced " + std::to_string(EntityHelper::EntitiesCount()));
						auto& sprite = created.AddComponent<SpriteRendererComponent>();
						sprite.TextureHandle = TextureManager::GetDefaultTexture(DefaultTexture::_9Sliced);
						SelectEntity(created.GetHandle());
					}
					if (ImGui::MenuItem("Pixel"))
					{
						Entity created = scene.CreateEntity("Pixel " + std::to_string(EntityHelper::EntitiesCount()));
						auto& sprite = created.AddComponent<SpriteRendererComponent>();
						sprite.TextureHandle = TextureManager::GetDefaultTexture(DefaultTexture::Pixel);
						SelectEntity(created.GetHandle());
					}
					if (ImGui::MenuItem("Logo"))
					{
						Entity created = scene.CreateEntity("Logo " + std::to_string(EntityHelper::EntitiesCount()));
						auto& sprite = created.AddComponent<SpriteRendererComponent>();
						sprite.TextureHandle = TextureManager::LoadTexture("Game/logo.png");
						SelectEntity(created.GetHandle());
					}

					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("Physics")) {
					if(ImGui::MenuItem("Dynamic Body"))
					{
						Entity created = scene.CreateEntity("Dynamic Body " + std::to_string(EntityHelper::EntitiesCount()));
						created.AddComponent<SpriteRendererComponent>();
						created.AddComponent<Rigidbody2DComponent>();
						created.AddComponent<BoxCollider2DComponent>();
						SelectEntity(created.GetHandle());
					}
					if (ImGui::MenuItem("Kinematic Body"))
					{
						Entity created = scene.CreateEntity("Kinematic Body " + std::to_string(EntityHelper::EntitiesCount()));
						created.AddComponent<SpriteRendererComponent>();
						created.AddComponent<Rigidbody2DComponent>().SetBodyType(BodyType::Kinematic);
						created.AddComponent<BoxCollider2DComponent>();
						SelectEntity(created.GetHandle());
					}
					if (ImGui::MenuItem("Static Body"))
					{
						Entity created = scene.CreateEntity("Static Body " + std::to_string(EntityHelper::EntitiesCount()));
						created.AddComponent<SpriteRendererComponent>();
						created.AddComponent<Rigidbody2DComponent>().SetBodyType(BodyType::Static);
						created.AddComponent<BoxCollider2DComponent>();
						SelectEntity(created.GetHandle());
					}

					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("Effects"))
				{
					if (ImGui::MenuItem("Particle System"))
					{
						Entity created = scene.CreateEntity("Particle System " + std::to_string(EntityHelper::EntitiesCount()));
						created.AddComponent<ParticleSystem2DComponent>();
						SelectEntity(created.GetHandle());
					}
					ImGui::EndMenu();
				}

				ImGui::EndMenu();
			}

			if (ImGui::MenuItem("Camera"))
			{
				Entity created = scene.CreateEntity("Camera " + std::to_string(EntityHelper::EntitiesCount()));
				created.AddComponent<Camera2DComponent>();
				SelectEntity(created.GetHandle());
			}

			if (ImGui::BeginMenu("UI"))
			{
				if (ImGui::MenuItem("Panel")) {
					Entity created = EntityHelper::CreateUIPanel(scene);
					SelectEntity(created.GetHandle());
				}
				if (ImGui::MenuItem("Image")) {
					Entity created = EntityHelper::CreateImageEntity(scene);
					if (!created.HasComponent<NameComponent>()) {
						created.AddComponent<NameComponent>(NameComponent("Image"));
					}
					SelectEntity(created.GetHandle());
				}
				if (ImGui::MenuItem("Text")) {
					Entity created = scene.CreateEntity("Text");
					created.AddComponent<RectTransformComponent>();
					created.AddComponent<TextRendererComponent>();
					SelectEntity(created.GetHandle());
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Button")) {
					Entity created = EntityHelper::CreateUIButton(scene);
					SelectEntity(created.GetHandle());
				}
				if (ImGui::MenuItem("Slider")) {
					Entity created = EntityHelper::CreateUISlider(scene);
					SelectEntity(created.GetHandle());
				}
				if (ImGui::MenuItem("Input Field")) {
					Entity created = EntityHelper::CreateUIInputField(scene);
					SelectEntity(created.GetHandle());
				}
				if (ImGui::MenuItem("Dropdown")) {
					Entity created = EntityHelper::CreateUIDropdown(scene);
					SelectEntity(created.GetHandle());
				}
				if (ImGui::MenuItem("Toggle")) {
					Entity created = EntityHelper::CreateUIToggle(scene);
					SelectEntity(created.GetHandle());
				}
				ImGui::EndMenu();
			}

			ImGui::EndPopup();
		}

		// Iterate all loaded scenes
		auto loadedScenes = SceneManager::Get().GetLoadedScenes();
		std::string sceneToRemove;

		for (auto& weakScene : loadedScenes) {
			auto scenePtr = weakScene.lock();
			if (!scenePtr) continue;
			Scene& scene = *scenePtr;

			ImGui::PushID(static_cast<int>(static_cast<uint64_t>(scene.GetSceneId())));

			ImGuiTreeNodeFlags sceneFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow
				| ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed;
			const std::string fullSceneLabel = scene.IsDirty() ? scene.GetName() + " *" : scene.GetName();
			bool sceneLabelTruncated = false;
			const std::string sceneLabel = ImGuiUtils::Ellipsize(fullSceneLabel, ImGui::GetContentRegionAvail().x, &sceneLabelTruncated);
			bool sceneOpen = ImGui::TreeNodeEx(sceneLabel.c_str(), sceneFlags);
			if (sceneLabelTruncated && ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", fullSceneLabel.c_str());
			}
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
				SelectSceneNode();
			}

			// Right-click context menu on scene tree node
			if (ImGui::BeginPopupContextItem()) {
				if (loadedScenes.size() > 1) {
					if (ImGui::MenuItem("Remove")) {
						sceneToRemove = scene.GetName();
					}
				} else {
					ImGui::MenuItem("Remove", nullptr, false, false);
				}
				ImGui::EndPopup();
			}

			if (sceneOpen) {
				auto view = scene.GetRegistry().view<entt::entity>();

				// Rebuild entity order if size changed (scene load, entity create/destroy)
				{
					std::vector<entt::entity> viewEntities(view.begin(), view.end());
					if (m_EntityOrder.size() != viewEntities.size()) {
						m_EntityOrder = viewEntities;
					} else {
						std::unordered_set<uint32_t> viewSet;
						for (auto e : viewEntities) viewSet.insert(static_cast<uint32_t>(e));
						m_EntityOrder.erase(
							std::remove_if(m_EntityOrder.begin(), m_EntityOrder.end(),
								[&](entt::entity e) { return viewSet.find(static_cast<uint32_t>(e)) == viewSet.end(); }),
							m_EntityOrder.end());
						std::unordered_set<uint32_t> orderSet;
						for (auto e : m_EntityOrder) orderSet.insert(static_cast<uint32_t>(e));
						for (auto e : viewEntities) {
							if (orderSet.find(static_cast<uint32_t>(e)) == orderSet.end())
								m_EntityOrder.push_back(e);
						}
					}
				}

				// DFS reorder so each child appears immediately after its
				// parent in the flat list — that lets the existing single-
				// loop renderer show parent-child nesting just by tracking
				// depth and calling ImGui::Indent. Roots come first in
				// the user's chosen m_EntityOrder; their subtrees follow
				// in the order each parent's HierarchyComponent::Children
				// lists them.
				std::vector<int> entityDepths;
				entityDepths.reserve(m_EntityOrder.size());
				{
					auto& reg = scene.GetRegistry();
					std::vector<entt::entity> reordered;
					reordered.reserve(m_EntityOrder.size());
					std::unordered_set<uint32_t> emitted;

					std::function<void(EntityHandle, int)> emitSubtree =
						[&](EntityHandle e, int depth) {
							if (!reg.valid(e)) return;
							const uint32_t key = static_cast<uint32_t>(e);
							if (!emitted.insert(key).second) return; // already in tree
							reordered.push_back(e);
							entityDepths.push_back(depth);
							if (reg.all_of<HierarchyComponent>(e)) {
								for (EntityHandle child : reg.get<HierarchyComponent>(e).Children) {
									emitSubtree(child, depth + 1);
								}
							}
						};

					// Pass 1: emit each root-of-subtree from the user-ordered
					// list. Skip entities that already appeared as a child.
					for (EntityHandle e : m_EntityOrder) {
						if (!reg.valid(e)) continue;
						EntityHandle parent = entt::null;
						if (reg.all_of<HierarchyComponent>(e)) {
							parent = reg.get<HierarchyComponent>(e).Parent;
						}
						if (parent == entt::null) {
							emitSubtree(e, 0);
						}
					}
					// Pass 2: anything still missing — orphaned children
					// whose parent is not in the registry — render flat.
					for (EntityHandle e : m_EntityOrder) {
						if (!reg.valid(e)) continue;
						const uint32_t key = static_cast<uint32_t>(e);
						if (emitted.contains(key)) continue;
						emitSubtree(e, 0);
					}
					m_EntityOrder = std::move(reordered);
				}

				int currentIndentDepth = 0;
				for (int entityIdx = 0; entityIdx < static_cast<int>(m_EntityOrder.size()); entityIdx++) {
					const EntityHandle entityHandle = m_EntityOrder[entityIdx];
					if (!scene.IsValid(entityHandle)) continue;
					Entity entity = scene.GetEntity(entityHandle);
					const bool selected = IsEntitySelected(entityHandle);

					// Step indent up/down to match this entity's depth.
					const int targetDepth = entityIdx < static_cast<int>(entityDepths.size())
						? entityDepths[entityIdx] : 0;
					while (currentIndentDepth < targetDepth) {
						ImGui::Indent(14.0f);
						++currentIndentDepth;
					}
					while (currentIndentDepth > targetDepth) {
						ImGui::Unindent(14.0f);
						--currentIndentDepth;
					}

					ImGui::PushID(static_cast<int>(static_cast<uint32_t>(entityHandle)));

					bool entityIsDisabled = scene.HasComponent<DisabledTag>(entityHandle);
					if (entityIsDisabled)
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));

					// Prefab-instance tint: amber if the source GUID can't be resolved
					// (orphaned), accent-blue otherwise. Engine is flat today so all
					// prefab-origin entities get the same color — no root vs child
					// distinction is possible until parent/child components land.
					bool entityIsPrefabTinted = false;
					if (!entityIsDisabled && scene.GetEntityOrigin(entityHandle) == EntityOrigin::Prefab) {
						const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(entityHandle));
						const bool resolvable = prefabGuid != 0 && !AssetRegistry::ResolvePath(prefabGuid).empty();
						ImGui::PushStyleColor(ImGuiCol_Text,
							resolvable ? EditorTheme::Colors::PrefabInstance : EditorTheme::Colors::PrefabOrphan);
						entityIsPrefabTinted = true;
					}

					if (m_RenamingEntity == entityHandle) {
						m_EntityRenameFrameCounter++;

						ImGui::PushItemWidth(-1);
						if (m_EntityRenameFrameCounter == 1) {
							ImGui::SetKeyboardFocusHere();
						}

						bool committed = ImGui::InputText("##EntityRename", m_EntityRenameBuffer, sizeof(m_EntityRenameBuffer),
							ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

						if (committed) {
							std::string newName(m_EntityRenameBuffer);
							if (!newName.empty() && entity.HasComponent<NameComponent>()) {
								entity.GetComponent<NameComponent>().Name = newName;
								scene.MarkDirty();
							}
							m_RenamingEntity = entt::null;
							m_EntityRenameFrameCounter = 0;
						}
						else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
							m_RenamingEntity = entt::null;
							m_EntityRenameFrameCounter = 0;
						}
						else if (m_EntityRenameFrameCounter > 2 && !ImGui::IsItemActive()) {
							std::string newName(m_EntityRenameBuffer);
							if (!newName.empty() && entity.HasComponent<NameComponent>()) {
								entity.GetComponent<NameComponent>().Name = newName;
								scene.MarkDirty();
							}
							m_RenamingEntity = entt::null;
							m_EntityRenameFrameCounter = 0;
						}

						ImGui::PopItemWidth();
					}
					else {
						bool entityLabelTruncated = false;
						const std::string entityLabel = ImGuiUtils::Ellipsize(entity.GetName(), ImGui::GetContentRegionAvail().x, &entityLabelTruncated);
						ImGui::Selectable(entityLabel.c_str(), selected);
						if (entityLabelTruncated && ImGui::IsItemHovered()) {
							ImGui::SetTooltip("%s", entity.GetName().c_str());
						}
						if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
							m_PressedEntity = entityHandle;
						}
						if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
							if (ImGui::IsItemHovered() && m_PressedEntity == entityHandle && !IsLeftMouseDragPastClickThreshold()) {
								const ImGuiIO& io = ImGui::GetIO();
								if (io.KeyShift) {
									SelectEntityRange(entityIdx);
								}
								else if (io.KeyCtrl) {
									ToggleEntitySelection(entityHandle, entityIdx);
								}
								else {
									SetSingleEntitySelection(entityHandle, entityIdx);
								}
							}
							if (m_PressedEntity == entityHandle) {
								m_PressedEntity = entt::null;
							}
						}

						if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
							m_RenamingEntity = entityHandle;
							m_EntityRenameFrameCounter = 0;
							std::snprintf(m_EntityRenameBuffer, sizeof(m_EntityRenameBuffer), "%s", entity.GetName().c_str());
						}
					}

					// Drag-drop source: drag entity to reorder or to asset browser for prefab
					if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
						HierarchyDragData dragData{ entityIdx, static_cast<uint32_t>(entityHandle) };
						ImGui::SetDragDropPayload("HIERARCHY_ENTITY", &dragData, sizeof(dragData));
						ImGui::Text("Move: %s", entity.GetName().c_str());
						ImGui::EndDragDropSource();
					}

					// Drag-drop target: reorder entities, or accept .prefab from asset browser
					if (ImGui::BeginDragDropTarget()) {
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
							auto* dragData = static_cast<const HierarchyDragData*>(payload->Data);
							int srcIndex = dragData->Index;
							if (srcIndex != entityIdx && srcIndex >= 0 && srcIndex < static_cast<int>(m_EntityOrder.size())) {
								entt::entity moved = m_EntityOrder[srcIndex];
								m_EntityOrder.erase(m_EntityOrder.begin() + srcIndex);
								int insertAt = (srcIndex < entityIdx) ? entityIdx - 1 : entityIdx;
								m_EntityOrder.insert(m_EntityOrder.begin() + insertAt, moved);
							}
						}
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
							std::string droppedPath(static_cast<const char*>(payload->Data));
							std::string ext = std::filesystem::path(droppedPath).extension().string();
							std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
							if (ext == ".prefab") {
								SceneSerializer::LoadEntityFromFile(scene, droppedPath);
								m_EntityOrder.clear();
							}
						}
						ImGui::EndDragDropTarget();
					}

					if (ImGui::BeginPopupContextItem())
					{
						if (!IsEntitySelected(entityHandle)) {
							SetSingleEntitySelection(entityHandle, entityIdx);
						}

						if (ImGui::MenuItem("Delete Entity"))
						{
							DeleteSelectedEntity(scene);
						}

						if (ImGui::MenuItem("Duplicate"))
						{
							DuplicateSelectedEntity(scene);
						}

						if (ImGui::MenuItem("Rename"))
						{
							BeginRenameSelectedEntity(scene);
						}

						// "Unpack Prefab" only shows up when at least one
						// selected entity is currently a prefab instance —
						// no point cluttering the menu otherwise.
						bool anySelectedIsPrefabInstance = false;
						for (EntityHandle h : GetSelectedEntities(scene)) {
							if (scene.HasComponent<PrefabInstanceComponent>(h)) {
								anySelectedIsPrefabInstance = true;
								break;
							}
						}
						if (anySelectedIsPrefabInstance) {
							ImGui::Separator();
							if (ImGui::MenuItem("Unpack Prefab")) {
								UnpackSelectedPrefabs(scene);
							}
						}

						ImGui::EndPopup();
					}

					if (entityIsPrefabTinted)
						ImGui::PopStyleColor();
					if (entityIsDisabled)
						ImGui::PopStyleColor();

					ImGui::PopID();
				}

				// Pop any remaining indents from nested entities so the
				// next scene tree node starts cleanly at depth 0.
				while (currentIndentDepth > 0) {
					ImGui::Unindent(14.0f);
					--currentIndentDepth;
				}

				ImGui::TreePop();
			}

			ImGui::PopID();
		}

		// Deferred scene removal (after iteration)
		if (!sceneToRemove.empty()) {
			ClearEntitySelection();
			SceneManager::Get().UnloadScene(sceneToRemove);
		}

		// Drag-drop target: only present during an active drag so it doesn't block right-click menus
		if (ImGui::GetDragDropPayload() != nullptr) {
			ImVec2 avail = ImGui::GetContentRegionAvail();
			if (avail.y > 0) {
				ImGui::InvisibleButton("##SceneDropTarget", ImVec2(-1, avail.y));
				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
						std::string droppedPath(static_cast<const char*>(payload->Data));
						std::string ext = std::filesystem::path(droppedPath).extension().string();
						std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
						if (ext == ".scene") {
							m_PendingSceneFileDrop = droppedPath;
						}
						else if (ext == ".prefab") {
							Scene* dropScene = SceneManager::Get().GetActiveScene();
							if (dropScene) {
								SceneSerializer::LoadEntityFromFile(*dropScene, droppedPath);
								m_EntityOrder.clear();
							}
						}
						else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
							Scene* dropScene = SceneManager::Get().GetActiveScene();
							if (dropScene) {
								std::string entityName = std::filesystem::path(droppedPath).stem().string();
								Entity newEntity = dropScene->CreateEntity(entityName);
								auto& sr = newEntity.AddComponent<SpriteRendererComponent>();
								sr.TextureHandle = TextureManager::LoadTexture(droppedPath);

								Texture2D* tex = TextureManager::GetTexture(sr.TextureHandle);
								if (tex && tex->IsValid() && tex->GetHeight() > 0) {
									float aspect = (float)tex->GetWidth() / (float)tex->GetHeight();
									auto& transform = newEntity.GetComponent<Transform2DComponent>();
									transform.Scale = { aspect, 1.0f };
								}

								SelectEntity(newEntity.GetHandle());
								dropScene->MarkDirty();
							}
						}
					}
					ImGui::EndDragDropTarget();
				}
			}
		}

		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)
			&& ImGui::IsMouseReleased(ImGuiMouseButton_Left)
			&& !IsLeftMouseDragPastClickThreshold()
			&& !ImGui::IsAnyItemHovered()) {
			ClearEntitySelection();
		}

		m_IsEntitiesPanelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		ImGui::End();
	}

	void ImGuiEditorLayer::RenderSceneSystemsInspector(Scene& scene) {
		ImGui::Text("Scene: %s", scene.GetName().c_str());
		ImGui::SeparatorText("Systems");

		const auto& systems = scene.GetGameSystemClassNames();
		for (size_t i = 0; i < systems.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			ImGui::TextUnformatted(systems[i].c_str());

			ImGui::SameLine();
			if (ImGui::SmallButton("^") && i > 0) {
				scene.MoveGameSystem(i, i - 1);
			}

			ImGui::SameLine();
			if (ImGui::SmallButton("v") && i + 1 < systems.size()) {
				scene.MoveGameSystem(i, i + 1);
			}

			ImGui::SameLine();
			if (ImGui::SmallButton("Remove")) {
				scene.RemoveGameSystem(i);
				ImGui::PopID();
				break;
			}
			ImGui::PopID();
		}

		// Pre-collect available systems so the button can be disabled when nothing
		// can be added — avoids the empty-popup sliver that looks broken.
		std::vector<EditorScriptDiscovery::ScriptEntry> scriptEntries;
		EditorScriptDiscovery::CollectProjectScriptEntries(scriptEntries);
		size_t availableSystemCount = 0;
		for (const auto& entry : scriptEntries) {
			if (entry.IsGameSystem && !scene.HasGameSystem(entry.ClassName)) {
				++availableSystemCount;
			}
		}
		const bool hasAvailableSystem = availableSystemCount > 0;

		float buttonWidth = ImGui::GetContentRegionAvail().x;
		if (!hasAvailableSystem) ImGui::BeginDisabled();
		if (ImGui::Button("+ Add System", ImVec2(buttonWidth, 0))) {
			ImGui::OpenPopup("AddSystemPopup");
			m_SystemSearchBuffer[0] = '\0';
		}
		if (!hasAvailableSystem) {
			ImGui::EndDisabled();
			// AllowWhenDisabled so hover still fires after BeginDisabled.
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
				ImGui::SetTooltip("No game systems available.\nCreate one via Asset Browser > Create > Scripting > GameSystem (C#).");
			}
		}

		if (ImGui::BeginPopup("AddSystemPopup")) {
			ImGui::SetNextItemWidth(-1);
			ImGui::InputTextWithHint("##SystemSearch", "Search systems...",
				m_SystemSearchBuffer, sizeof(m_SystemSearchBuffer));
			ImGui::Separator();

			std::string filter(m_SystemSearchBuffer);
			std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

			for (const auto& scriptEntry : scriptEntries) {
				if (!scriptEntry.IsGameSystem || scene.HasGameSystem(scriptEntry.ClassName)) {
					continue;
				}

				if (!filter.empty()) {
					std::string lowerClassName = EditorScriptDiscovery::ToLowerCopy(scriptEntry.ClassName);
					std::string lowerPath = EditorScriptDiscovery::ToLowerCopy(scriptEntry.Path.string());
					if (lowerClassName.find(filter) == std::string::npos
						&& lowerPath.find(filter) == std::string::npos) {
						continue;
					}
				}

				const std::string label = BuildScriptMenuLabel(scriptEntry);
				const std::string path = scriptEntry.Path.string();
				if (ImGuiUtils::MenuItemEllipsis(label, path.c_str(), nullptr, false, true, 260.0f)) {
					scene.AddGameSystem(scriptEntry.ClassName);
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndPopup();
		}
	}

	void ImGuiEditorLayer::RenderInspectorPanel(Scene& scene) {
		ImGui::Begin("Inspector");

		// Materialize the selection set (filters dropped entities). When N == 0
		// fall through to the asset / scene-systems panels exactly like before.
		std::vector<EntityHandle> selectedHandles = GetSelectedEntities(scene);
		if (selectedHandles.empty()) {
			m_SelectedEntity = entt::null;
			if (m_IsSceneNodeSelected) {
				RenderSceneSystemsInspector(scene);
			}
			else {
				RenderAssetInspector();
			}
			m_IsInspectorPanelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
			ImGui::End();
			return;
		}

		// Stable Entity wrappers for the selection — reused for the component
		// intersection and passed to every multi-edit inspector below.
		std::vector<Entity> selectedEntities;
		selectedEntities.reserve(selectedHandles.size());
		for (EntityHandle h : selectedHandles) {
			selectedEntities.push_back(scene.GetEntity(h));
		}
		std::span<const Entity> entitySpan(selectedEntities);
		Entity entity = selectedEntities[0]; // primary, used for the drag-source label and prefab buttons

		// Keep m_SelectedEntity pointed at a valid handle — some callers
		// (prefab Apply / Revert, focus / duplicate / rename helpers) still
		// index it directly.
		if (!scene.IsValid(m_SelectedEntity)) {
			m_SelectedEntity = entity.GetHandle();
		}

		// ── Entity Header: Name + Toggles ──────────────────

		// Editable Name. With multi-select, mixed names display "—" via the
		// hint and edits propagate to all entities (creating NameComponent
		// when missing).
		bool nameUniform = true;
		std::string firstName = entity.HasComponent<NameComponent>()
			? entity.GetComponent<NameComponent>().Name : std::string("Entity");
		for (std::size_t i = 1; i < selectedEntities.size(); ++i) {
			std::string n = selectedEntities[i].HasComponent<NameComponent>()
				? selectedEntities[i].GetComponent<NameComponent>().Name : std::string("Entity");
			if (n != firstName) { nameUniform = false; break; }
		}
		char nameBuf[256]{};
		if (nameUniform) {
			std::snprintf(nameBuf, sizeof(nameBuf), "%s", firstName.c_str());
		}
		ImGui::SetNextItemWidth(-1);
		const bool nameSubmitted = nameUniform
			? ImGui::InputText("##EntityName", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_EnterReturnsTrue)
			: ImGui::InputTextWithHint("##EntityName", "-", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
		if (nameSubmitted) {
			std::string newName(nameBuf);
			for (Entity& e : selectedEntities) {
				if (e.HasComponent<NameComponent>()) {
					e.GetComponent<NameComponent>().Name = newName;
				}
				else {
					scene.AddComponent<NameComponent>(e.GetHandle(), newName);
				}
			}
			scene.MarkDirty();
		}

		// Toggles row: Enabled + Static. Tri-state when the underlying tag
		// presence differs across the selection. Editing applies to all.
		{
			constexpr int kMixedValueFlag = 1 << 12;

			auto sampleHas = [&](auto checkFn) -> std::pair<bool, bool> {
				bool first = checkFn(selectedEntities[0]);
				bool uniform = true;
				for (std::size_t i = 1; i < selectedEntities.size(); ++i) {
					if (checkFn(selectedEntities[i]) != first) {
						uniform = false;
						break;
					}
				}
				return { first, uniform };
			};

			auto enabledSample = sampleHas([](Entity& e) { return !e.HasComponent<DisabledTag>(); });
			bool isEnabled = enabledSample.first;
			if (!enabledSample.second) ImGui::PushItemFlag(static_cast<ImGuiItemFlags>(kMixedValueFlag), true);
			const bool enabledChanged = ImGui::Checkbox("Enabled", &isEnabled);
			if (!enabledSample.second) ImGui::PopItemFlag();
			if (enabledChanged) {
				for (Entity& e : selectedEntities) {
					if (isEnabled && e.HasComponent<DisabledTag>()) e.RemoveComponent<DisabledTag>();
					else if (!isEnabled && !e.HasComponent<DisabledTag>()) e.AddComponent<DisabledTag>();
				}
				scene.MarkDirty();
			}

			ImGui::SameLine();

			auto staticSample = sampleHas([](Entity& e) { return e.HasComponent<StaticTag>(); });
			bool isStatic = staticSample.first;
			if (!staticSample.second) ImGui::PushItemFlag(static_cast<ImGuiItemFlags>(kMixedValueFlag), true);
			const bool staticChanged = ImGui::Checkbox("Static", &isStatic);
			if (!staticSample.second) ImGui::PopItemFlag();
			if (staticChanged) {
				for (Entity& e : selectedEntities) {
					if (isStatic && !e.HasComponent<StaticTag>()) e.AddComponent<StaticTag>();
					else if (!isStatic && e.HasComponent<StaticTag>()) e.RemoveComponent<StaticTag>();
				}
				scene.MarkDirty();
			}
		}

		/* INFO(Ben-Scr): MetaData Doesn't need to be displayed
		if (const EntityMetaData* metaData = entity.GetMetaData()) {
			const char* originText = "Runtime";
			if (metaData->Origin == EntityOrigin::Scene) originText = "Scene";
			if (metaData->Origin == EntityOrigin::Prefab) originText = "Prefab";
			ImGui::TextDisabled("Origin: %s", originText);

			const uint64_t runtimeId = metaData->RuntimeID;
			ImGui::TextDisabled("RuntimeID: %llu", runtimeId);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to copy");
			if (ImGui::IsItemClicked()) ImGui::SetClipboardText(std::to_string(runtimeId).c_str());

			if (metaData->Origin == EntityOrigin::Scene && static_cast<uint64_t>(metaData->SceneGUID) != 0) {
				const uint64_t sceneGuid = static_cast<uint64_t>(metaData->SceneGUID);
				ImGui::TextDisabled("SceneGUID: %llu", sceneGuid);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to copy");
				if (ImGui::IsItemClicked()) ImGui::SetClipboardText(std::to_string(sceneGuid).c_str());
			}
			if (metaData->Origin == EntityOrigin::Prefab && static_cast<uint64_t>(metaData->PrefabGUID) != 0) {
				const uint64_t prefabGuid = static_cast<uint64_t>(metaData->PrefabGUID);
				ImGui::TextDisabled("PrefabGUID: %llu", prefabGuid);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to copy");
				if (ImGui::IsItemClicked()) ImGui::SetClipboardText(std::to_string(prefabGuid).c_str());

				if (ImGui::Button("Apply to Prefab")) {
					SceneSerializer::ApplyPrefabInstanceOverrides(scene, m_SelectedEntity);
				}
				ImGui::SameLine();
				if (ImGui::Button("Revert All")) {
					EntityHandle replacement = SceneSerializer::RevertPrefabInstanceOverride(scene, m_SelectedEntity, {});
					if (replacement != entt::null) {
						SelectEntity(replacement);
						m_EntityOrder.clear();
					}
				}
			}
		}
		*/

		m_IsInspectorPanelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		ImGui::Separator();

		const auto& registry = SceneManager::Get().GetComponentRegistry();

		std::type_index pendingRemoval = typeid(void);

		// Prefab override state for the inspector header / per-component badges.
		// Computed once per frame for the primary entity when the selection is
		// a single prefab instance. Multi-select + prefab overrides is out of
		// scope for v1 — when more than one entity is selected the UI behaves
		// as if no overrides exist (consistent with mixed-value semantics).
		Json::Value prefabOverrides = Json::Value::MakeObject();
		const bool isSinglePrefabInstance = selectedEntities.size() == 1
			&& scene.GetEntityOrigin(entity.GetHandle()) == EntityOrigin::Prefab
			&& static_cast<uint64_t>(scene.GetPrefabGUID(entity.GetHandle())) != 0;
		bool prefabSourceResolvable = false;
		if (isSinglePrefabInstance) {
			prefabSourceResolvable = SceneSerializer::ComputeInstanceOverrides(
				scene, entity.GetHandle(), prefabOverrides);
		}

		// Top-of-inspector prefab actions (only when the source resolves; orphans
		// can't apply or revert because we have no source to diff against).
		if (isSinglePrefabInstance && prefabSourceResolvable) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.42f, 0.66f, 0.95f, 1.0f));
			ImGui::TextUnformatted("Prefab Instance");
			ImGui::PopStyleColor();
			if (ImGui::SmallButton("Apply All")) {
				// Capture old source BEFORE the apply, then push instance state to disk
				// and propagate to other live instances preserving their overrides.
				const std::string prefabPath = AssetRegistry::ResolvePath(
					static_cast<uint64_t>(scene.GetPrefabGUID(entity.GetHandle())));
				Json::Value previousSourceEntity;
				bool havePrev = false;
				if (!prefabPath.empty() && File::Exists(prefabPath)) {
					Json::Value previousRoot;
					std::string parseError;
					if (Json::TryParse(File::ReadAllText(prefabPath), previousRoot, &parseError) && previousRoot.IsObject()) {
						if (const Json::Value* eb = previousRoot.FindMember("Entity")) { previousSourceEntity = *eb; havePrev = true; }
						else if (const Json::Value* lb = previousRoot.FindMember("prefab")) { previousSourceEntity = *lb; havePrev = true; }
					}
				}
				if (SceneSerializer::ApplyPrefabInstanceOverrides(scene, entity.GetHandle()) && havePrev) {
					const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(entity.GetHandle()));
					SceneManager::Get().ForeachLoadedScene([&](Scene& s) {
						std::vector<EntityHandle> targets;
						auto view = s.GetRegistry().view<EntityMetaDataComponent>();
						for (entt::entity e2 : view) {
							const auto& meta = view.get<EntityMetaDataComponent>(e2).MetaData;
							if (meta.Origin != EntityOrigin::Prefab) continue;
							if (static_cast<uint64_t>(meta.PrefabGUID) != prefabGuid) continue;
							if (e2 == entity.GetHandle() && &s == &scene) continue; // skip the just-applied entity
							targets.push_back(e2);
						}
						bool anyRefreshed = false;
						for (EntityHandle t : targets) {
							if (SceneSerializer::RefreshPrefabInstance(s, t, previousSourceEntity) != entt::null) {
								anyRefreshed = true;
							}
						}
						if (anyRefreshed) s.MarkDirty();
					});
				}
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Revert All")) {
				EntityHandle replacement = SceneSerializer::RevertPrefabInstanceOverride(scene, entity.GetHandle(), {});
				if (replacement != entt::null) {
					SelectEntity(replacement);
					m_EntityOrder.clear();
				}
			}
			ImGui::Separator();
		}
		else if (isSinglePrefabInstance) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.70f, 0.30f, 1.0f));
			ImGui::TextUnformatted("Prefab Instance (orphaned — source missing)");
			ImGui::PopStyleColor();
			ImGui::Separator();
		}

		// Lambda: do any override paths fall under this component's serializedName?
		auto componentHasOverrides = [&](const std::string& serializedName) -> bool {
			if (!prefabSourceResolvable || serializedName.empty()) return false;
			const std::string prefix = serializedName + ".";
			for (const auto& [path, _] : prefabOverrides.GetObject()) {
				if (path == serializedName || path.compare(0, prefix.size(), prefix) == 0) return true;
			}
			return false;
		};

		// Collect components per their presence across the selection. Only
		// components present on EVERY selected entity are shown; components
		// present on some-but-not-all are listed in a muted footer line.
		std::vector<std::string> hiddenPartialComponents;

		registry.ForEachComponentInfo([&](const std::type_index& typeId, const ComponentInfo& info) {
			if (info.category != ComponentCategory::Component) return;
			if (info.displayName == "Name") return; // Shown in entity header

			// Set-intersection check: skip when not all entities have it. If
			// some have it and others don't, remember it for the footer.
			std::size_t haveCount = 0;
			for (const Entity& e : selectedEntities) {
				if (info.has(e)) ++haveCount;
			}
			if (haveCount == 0) return;
			if (haveCount < selectedEntities.size()) {
				hiddenPartialComponents.push_back(info.displayName);
				return;
			}

			// Scripts render their own per-script sections — skip the outer wrapper
			if (info.displayName == "Scripts") {
				DispatchComponentInspector(info, entitySpan);
				return;
			}

			bool removeRequested = false;
			bool copyRequested = false;
			bool pasteRequested = false;
			bool resetRequested = false;
			ComponentClipboardPayload clipboardPayload;
			const bool hasComponentClipboard = TryReadComponentClipboard(m_ComponentClipboardJson, clipboardPayload);
			const ComponentInfo* pastedComponentInfo = hasComponentClipboard
				? FindComponentInfoBySerializedName(registry, clipboardPayload.SerializedName)
				: nullptr;
			const bool canPasteComponent = hasComponentClipboard
				&& pastedComponentInfo
				&& pastedComponentInfo->add
				&& pastedComponentInfo->has
				&& clipboardPayload.Data.IsObject();
			const bool canSerializeComponent = !info.serializedName.empty();

			const std::string componentSerializedName = info.serializedName;
			const bool thisComponentOverridden = componentHasOverrides(componentSerializedName);

			bool revertComponentRequested = false;
			bool applyComponentRequested = false;
			std::string revertFieldRequested; // empty = none

			bool open = ImGuiUtils::BeginComponentSection(info.displayName.c_str(), removeRequested, [&]() {
				if (ImGui::MenuItem("Copy Component", nullptr, false, canSerializeComponent)) {
					copyRequested = true;
				}
				if (ImGui::MenuItem("Paste Component", nullptr, false, canPasteComponent)) {
					pasteRequested = true;
				}
				if (ImGui::MenuItem("Reset Component", nullptr, false, canSerializeComponent)) {
					resetRequested = true;
				}
				// Prefab override actions — only meaningful when this entity is a
				// prefab instance whose source resolves AND this component has
				// at least one diff against the source.
				if (isSinglePrefabInstance && prefabSourceResolvable && thisComponentOverridden) {
					ImGui::Separator();
					if (ImGui::BeginMenu("Revert Field")) {
						const std::string prefix = componentSerializedName + ".";
						for (const auto& [path, _] : prefabOverrides.GetObject()) {
							if (path != componentSerializedName && path.compare(0, prefix.size(), prefix) != 0) continue;
							const char* leaf = path.c_str() + (path.size() > prefix.size() ? prefix.size() : 0);
							if (ImGui::MenuItem(leaf)) {
								revertFieldRequested = path;
							}
						}
						ImGui::EndMenu();
					}
					if (ImGui::MenuItem("Revert Component")) {
						revertComponentRequested = true;
					}
					if (ImGui::MenuItem("Apply Component")) {
						applyComponentRequested = true;
					}
				}
			});

			// Override badge — colored dot drawn next to the section header to
			// surface the diff at a glance. Sourced from EditorTheme so future
			// theme changes don't require touching the inspector.
			if (thisComponentOverridden) {
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Colors::OverrideMarker);
				ImGui::TextUnformatted(" *");
				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Component has per-field overrides relative to the source prefab.");
				}
			}

			// Component drag source on the header. References the primary
			// entity — useful when dragging onto a script field, since that
			// field would only be able to hold one component reference anyway.
			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
				const uint64_t entityRuntimeId = entity.GetRuntimeID();
				std::string refStr = std::to_string(entityRuntimeId) + ":" + info.displayName;
				ImGui::SetDragDropPayload("COMPONENT_REF", refStr.c_str(), refStr.size() + 1);
				ImGui::Text("Ref: %s.%s", entity.GetName().c_str(), info.displayName.c_str());
				ImGui::EndDragDropSource();
			}

			if (removeRequested) {
				pendingRemoval = typeId;
			}
			// Copy serializes the primary entity. Paste / Reset apply to ALL
			// selected entities — that matches the rest of the multi-edit
			// behavior (each entity that lacks the component gets it added).
			if (copyRequested && canSerializeComponent) {
				Json::Value componentValue = SceneSerializer::SerializeComponent(scene, entity.GetHandle(), info.serializedName);
				if (!componentValue.IsNull()) {
					Json::Value root = Json::Value::MakeObject();
					root.AddMember("type", Json::Value("ComponentClipboard"));
					root.AddMember("component", Json::Value(info.serializedName));
					root.AddMember("displayName", Json::Value(info.displayName));
					root.AddMember("data", std::move(componentValue));
					m_ComponentClipboardJson = Json::Stringify(root, false);
				}
			}
			if (pasteRequested && canPasteComponent) {
				bool anyApplied = false;
				for (const Entity& e : selectedEntities) {
					if (!pastedComponentInfo->has(e)) {
						pastedComponentInfo->add(e);
					}
					if (SceneSerializer::DeserializeComponent(scene, e.GetHandle(),
						clipboardPayload.SerializedName, clipboardPayload.Data)) {
						anyApplied = true;
					}
				}
				if (anyApplied) scene.MarkDirty();
			}
			if (resetRequested && canSerializeComponent) {
				bool anyReset = false;
				for (const Entity& e : selectedEntities) {
					if (SceneSerializer::ResetComponent(scene, e.GetHandle(), info.serializedName)) {
						anyReset = true;
					}
				}
				if (anyReset) scene.MarkDirty();
			}

			// Prefab override operations — single-instance only (multi-select
			// composition is intentionally deferred). Revert paths destroy and
			// re-create the entity, so the selection handle is refreshed below.
			if (!revertFieldRequested.empty()) {
				EntityHandle replacement = SceneSerializer::RevertPrefabInstanceOverride(
					scene, entity.GetHandle(), revertFieldRequested);
				if (replacement != entt::null) {
					SelectEntity(replacement);
					m_EntityOrder.clear();
				}
			}
			if (revertComponentRequested) {
				// Revert every override path under this component by walking the
				// captured overrides set. Each revert call destroys + recreates,
				// so we iterate paths first and only refresh the selection once.
				const std::string prefix = componentSerializedName + ".";
				std::vector<std::string> paths;
				for (const auto& [path, _] : prefabOverrides.GetObject()) {
					if (path == componentSerializedName || path.compare(0, prefix.size(), prefix) == 0) {
						paths.push_back(path);
					}
				}
				EntityHandle current = entity.GetHandle();
				for (const std::string& p : paths) {
					EntityHandle replacement = SceneSerializer::RevertPrefabInstanceOverride(scene, current, p);
					if (replacement != entt::null) current = replacement;
				}
				SelectEntity(current);
				m_EntityOrder.clear();
			}
			if (applyComponentRequested) {
				// "Apply Component" pushes the whole instance state to source
				// (no per-component partial save in v1) then propagates with
				// override-preservation, matching Apply All. The granularity
				// difference vs Apply All is purely UX framing.
				const std::string prefabPath = AssetRegistry::ResolvePath(
					static_cast<uint64_t>(scene.GetPrefabGUID(entity.GetHandle())));
				Json::Value previousSourceEntity;
				bool havePrev = false;
				if (!prefabPath.empty() && File::Exists(prefabPath)) {
					Json::Value previousRoot;
					std::string parseError;
					if (Json::TryParse(File::ReadAllText(prefabPath), previousRoot, &parseError) && previousRoot.IsObject()) {
						if (const Json::Value* eb = previousRoot.FindMember("Entity")) { previousSourceEntity = *eb; havePrev = true; }
						else if (const Json::Value* lb = previousRoot.FindMember("prefab")) { previousSourceEntity = *lb; havePrev = true; }
					}
				}
				if (SceneSerializer::ApplyPrefabInstanceOverrides(scene, entity.GetHandle()) && havePrev) {
					const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(entity.GetHandle()));
					SceneManager::Get().ForeachLoadedScene([&](Scene& s) {
						std::vector<EntityHandle> targets;
						auto view = s.GetRegistry().view<EntityMetaDataComponent>();
						for (entt::entity e2 : view) {
							const auto& meta = view.get<EntityMetaDataComponent>(e2).MetaData;
							if (meta.Origin != EntityOrigin::Prefab) continue;
							if (static_cast<uint64_t>(meta.PrefabGUID) != prefabGuid) continue;
							if (e2 == entity.GetHandle() && &s == &scene) continue;
							targets.push_back(e2);
						}
						bool anyRefreshed = false;
						for (EntityHandle t : targets) {
							if (SceneSerializer::RefreshPrefabInstance(s, t, previousSourceEntity) != entt::null) {
								anyRefreshed = true;
							}
						}
						if (anyRefreshed) s.MarkDirty();
					});
				}
			}

			if (open) {
				DispatchComponentInspector(info, entitySpan);
				ImGuiUtils::EndComponentSection();
			}
		});

		if (pendingRemoval != typeid(void)) {
			// Remove from EVERY selected entity that has it.
			registry.ForEachComponentInfo([&](const std::type_index& typeId, const ComponentInfo& info) {
				if (typeId != pendingRemoval || !info.remove) return;
				for (const Entity& e : selectedEntities) {
					if (info.has(e)) info.remove(e);
				}
			});
			scene.MarkDirty();
		}

		if (!hiddenPartialComponents.empty()) {
			std::string text = "Hidden: ";
			for (std::size_t i = 0; i < hiddenPartialComponents.size(); ++i) {
				if (i > 0) text += ", ";
				text += hiddenPartialComponents[i];
			}
			text += " - not on all selected";
			ImGui::TextDisabled("%s", text.c_str());
		}

		// Render the unified reference-picker popup once per inspector frame,
		// so any native component's PropertyDrawer-driven asset/entity picker
		// surfaces. The script inspector renders it inside its own draw too,
		// for the case where the panel is open without other components.
		ReferencePicker::RenderPopup();

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		float buttonWidth = ImGui::GetContentRegionAvail().x;
		if (ImGui::Button("Add Component", ImVec2(buttonWidth, 0))) {
			ImGui::OpenPopup("AddComponentPopup");
			m_ComponentSearchBuffer[0] = '\0';
		}

		// Add Component is offered as long as at least one selected entity is
		// missing it (so the menu doesn't go empty when the selection happens
		// to overlap on every component). Adding writes to every selected
		// entity that doesn't already have the component.
		auto componentMissingFromAny = [&](const ComponentInfo& info) -> bool {
			for (const Entity& e : selectedEntities) {
				if (!info.has(e)) return true;
			}
			return false;
		};
		auto addComponentToAll = [&](const ComponentInfo& info) {
			for (const Entity& e : selectedEntities) {
				if (!info.has(e)) info.add(e);
			}
			scene.MarkDirty();
		};
		// `proposed` would conflict on at least one selected entity if any of
		// its current components has a declared conflict with `proposed` (or
		// vice-versa). The Add Component popup hides such entries entirely so
		// the user can't end up in an invariant-violating state. The matching
		// existing component is reported via outConflictName for tooltips.
		auto componentConflictsWithSelection = [&](const std::type_index& proposed,
			std::string* outConflictName) -> bool
		{
			for (const Entity& e : selectedEntities) {
				if (registry.HasConflict(e, proposed)) {
					if (outConflictName) {
						registry.ForEachComponentInfo([&](const std::type_index& id, const ComponentInfo& info) {
							if (!outConflictName->empty()) return;
							if (id == proposed || !info.has || !info.has(e)) return;
							if (registry.TypesConflict(id, proposed)) *outConflictName = info.displayName;
						});
					}
					return true;
				}
			}
			return false;
		};

		if (ImGui::BeginPopup("AddComponentPopup")) {
			ImGui::SetNextItemWidth(-1);
			ImGui::InputTextWithHint("##CompSearch", "Search components...",
				m_ComponentSearchBuffer, sizeof(m_ComponentSearchBuffer));
			ImGui::Separator();

			std::string filter(m_ComponentSearchBuffer);
			std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

			bool hasFilter = !filter.empty();
			std::vector<EditorScriptDiscovery::ScriptEntry> scriptEntries;
			EditorScriptDiscovery::CollectProjectScriptEntries(scriptEntries);

			if (hasFilter) {
				// Flat filtered list when searching
				registry.ForEachComponentInfo([&](const std::type_index& typeId, const ComponentInfo& info) {
					if (info.category != ComponentCategory::Component) return;
					if (info.displayName == "Scripts") return;
					if (!componentMissingFromAny(info)) return;

					std::string lowerName = info.displayName;
					std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
					if (lowerName.find(filter) == std::string::npos) return;

					std::string conflictName;
					const bool conflicts = componentConflictsWithSelection(typeId, &conflictName);
					const bool enabled = !conflicts;
					if (ImGuiUtils::MenuItemEllipsis(info.displayName, info.displayName.c_str(), nullptr, false, enabled, 260.0f)) {
						if (enabled) addComponentToAll(info);
					}
					if (conflicts && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
						ImGui::SetTooltip("Conflicts with %s", conflictName.c_str());
					}
				});

				// Also search C# components and scripts by name.
				for (const auto& scriptEntry : scriptEntries) {
					if (scriptEntry.IsGameSystem || scriptEntry.IsGlobalSystem) {
						continue;
					}
					std::string lowerClassName = EditorScriptDiscovery::ToLowerCopy(scriptEntry.ClassName);
					std::string lowerPath = EditorScriptDiscovery::ToLowerCopy(scriptEntry.Path.string());
					if (lowerClassName.find(filter) == std::string::npos
						&& lowerPath.find(filter) == std::string::npos) {
						continue;
					}

					const std::string label = BuildScriptMenuLabel(scriptEntry);
					const std::string path = scriptEntry.Path.string();
					if (ImGuiUtils::MenuItemEllipsis(label, path.c_str(), nullptr, false, true, 260.0f)) {
						for (const Entity& e : selectedEntities) {
							if (scriptEntry.IsManagedComponent) {
								AttachManagedComponentToEntity(const_cast<Entity&>(e), scene, scriptEntry);
							}
							else {
								AttachScriptToEntity(const_cast<Entity&>(e), scene, scriptEntry);
							}
						}
					}
				}
			} else {
				// Categorized tree view
				// Collect components grouped by subcategory. Pair the
				// ComponentInfo* with its std::type_index so the leaf render
				// can ask the registry whether the type conflicts with
				// anything already on the selection.
				struct CategoryEntry { std::type_index TypeId; const ComponentInfo* Info; };
				std::vector<std::pair<std::string, std::vector<CategoryEntry>>> categories;
				std::unordered_map<std::string, size_t> categoryIndex;

				// Define subcategory ordering
				const std::vector<std::string> subcategoryOrder = {
					"General", "Rendering", "Physics", "Audio"
				};
				for (const auto& sub : subcategoryOrder) {
					categoryIndex[sub] = categories.size();
					categories.emplace_back(sub, std::vector<CategoryEntry>{});
				}

				registry.ForEachComponentInfo([&](const std::type_index& typeId, const ComponentInfo& info) {
					if (info.category != ComponentCategory::Component) return;
					if (info.displayName == "Scripts") return;
					if (!componentMissingFromAny(info)) return;

					std::string sub = info.subcategory.empty() ? "General" : info.subcategory;
					auto it = categoryIndex.find(sub);
					if (it == categoryIndex.end()) {
						categoryIndex[sub] = categories.size();
						categories.emplace_back(sub, std::vector<CategoryEntry>{});
						it = categoryIndex.find(sub);
					}
					categories[it->second].second.push_back({ typeId, &info });
				});

				for (const auto& [subcategory, components] : categories) {
					if (components.empty()) continue;

					if (ImGui::TreeNode(subcategory.c_str())) {
						for (const auto& entry : components) {
							const ComponentInfo* info = entry.Info;
							std::string conflictName;
							const bool conflicts = componentConflictsWithSelection(entry.TypeId, &conflictName);
							const bool enabled = !conflicts;
							if (ImGuiUtils::MenuItemEllipsis(info->displayName, info->displayName.c_str(), nullptr, false, enabled, 260.0f)) {
								if (enabled) addComponentToAll(*info);
							}
							if (conflicts && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
								ImGui::SetTooltip("Conflicts with %s", conflictName.c_str());
							}
						}
						ImGui::TreePop();
					}
				}

				// Managed components subcategory.
				bool hasManagedComponents = false;
				for (const auto& scriptEntry : scriptEntries) {
					if (scriptEntry.IsManagedComponent && !scriptEntry.IsGameSystem && !scriptEntry.IsGlobalSystem) {
						hasManagedComponents = true;
						break;
					}
				}
				if (hasManagedComponents) {
					if (ImGui::TreeNode("Components (C#)")) {
						for (const auto& scriptEntry : scriptEntries) {
							if (!scriptEntry.IsManagedComponent || scriptEntry.IsGameSystem || scriptEntry.IsGlobalSystem) {
								continue;
							}
							// Hide entries already attached to every entity in
							// the selection — leave it visible if at least one
							// entity is missing it.
							bool missingFromAny = false;
							for (const Entity& e : selectedEntities) {
								if (!e.HasComponent<ScriptComponent>()
									|| !e.GetComponent<ScriptComponent>().HasManagedComponent(scriptEntry.ClassName)) {
									missingFromAny = true;
									break;
								}
							}
							if (!missingFromAny) continue;
							const std::string label = BuildScriptMenuLabel(scriptEntry);
							const std::string path = scriptEntry.Path.string();
							if (ImGuiUtils::MenuItemEllipsis(label, path.c_str(), nullptr, false, true, 260.0f)) {
								for (const Entity& e : selectedEntities) {
									AttachManagedComponentToEntity(const_cast<Entity&>(e), scene, scriptEntry);
								}
							}
						}
						ImGui::TreePop();
					}
				}

				// Scripts subcategory: managed and native script types as addable items
				if (!scriptEntries.empty()) {
					if (ImGui::TreeNode("Scripts")) {
						for (const auto& scriptEntry : scriptEntries) {
							if (scriptEntry.IsManagedComponent || scriptEntry.IsGameSystem || scriptEntry.IsGlobalSystem) {
								continue;
							}
							const std::string label = BuildScriptMenuLabel(scriptEntry);
							const std::string path = scriptEntry.Path.string();
							if (ImGuiUtils::MenuItemEllipsis(label, path.c_str(), nullptr, false, true, 260.0f)) {
								for (const Entity& e : selectedEntities) {
									AttachScriptToEntity(const_cast<Entity&>(e), scene, scriptEntry);
								}
							}
						}
						ImGui::TreePop();
					}
				}
			}

			ImGui::EndPopup();
		}

		// Drag-drop target: accept script files dropped onto the Inspector panel
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
				std::string droppedPath(static_cast<const char*>(payload->Data));
				std::vector<EditorScriptDiscovery::ScriptEntry> droppedScripts;
				EditorScriptDiscovery::CollectScriptFile(std::filesystem::path(droppedPath), droppedScripts);
				for (const auto& scriptEntry : droppedScripts) {
					if (scriptEntry.IsGameSystem || scriptEntry.IsGlobalSystem) {
						continue;
					}
					for (const Entity& e : selectedEntities) {
						if (scriptEntry.IsManagedComponent) {
							AttachManagedComponentToEntity(const_cast<Entity&>(e), scene, scriptEntry);
						}
						else {
							AttachScriptToEntity(const_cast<Entity&>(e), scene, scriptEntry);
						}
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		// Mark the scene dirty only when ImGui reports a real value change on
		// the active widget this frame. IsAnyItemActive() would fire on mere
		// focus/click; ActiveIdHasBeenEditedThisFrame fires only on edits
		// (drag step, keystroke, etc.). Scope to this panel's focus stack.
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
			const ImGuiContext& g = *ImGui::GetCurrentContext();
			if (g.ActiveId != 0 && g.ActiveIdHasBeenEditedThisFrame) {
				scene.MarkDirty();
			}
		}
		m_IsInspectorPanelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		ImGui::End();
	}
}
