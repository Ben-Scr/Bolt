#include <pch.hpp>
#include "ImGuiEditorLayer.hpp"

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

namespace Bolt {
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
	}



	// ──────────────────────────────────────────────
	//  Lifecycle
	// ──────────────────────────────────────────────

	void ImGuiEditorLayer::OnAttach(Application& app) {
		Application::SetIsPlaying(false);
		Application::SetGameInputEnabled(false);
		Gizmo::SetShowInRuntime(false);
		if (app.GetRenderer2D()) {
			app.GetRenderer2D()->SetSkipBeginFrameRender(true);
		}

		if (m_LogSubscriptionId.value != 0) {
			Log::OnLog.Remove(m_LogSubscriptionId);
		}

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
		Application::SetGameInputEnabled(true);
		Gizmo::SetShowInRuntime(true);
		if (m_LogSubscriptionId.value != 0) {
			Log::OnLog.Remove(m_LogSubscriptionId);
			m_LogSubscriptionId = EventId{};
		}
		m_LogDispatchState.reset();
		ClearPreviewTextureCache();

		DestroyFBO(m_EditorViewFBO);
		DestroyFBO(m_GameViewFBO);
		EditorIcons::Shutdown();
		m_AssetBrowser.Shutdown();
		m_PackageManagerPanel.Shutdown();
		m_PackageManager.Shutdown();
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
		const bool hasEntitySelection = m_SelectedEntity != entt::null && scene.IsValid(m_SelectedEntity);

		if (input.GetKeyDown(KeyCode::F) && hasShortcutFocus && hasEntitySelection) {
			FocusSelectedEntity(scene);
		}

		if (Application::GetIsPlaying() || !hasShortcutFocus || !hasEntitySelection) {
			return;
		}

		if (m_RenamingEntity != entt::null || ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive()) {
			return;
		}

		if (input.GetKey(KeyCode::LeftControl) && input.GetKeyDown(KeyCode::D)) {
			DuplicateSelectedEntity(scene);
		}
		if (input.GetKeyDown(KeyCode::Delete)) {
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

		ImGuiID dockspaceId = ImGui::GetID("BoltEditorDockspace");
		ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
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
				BoltProject* project = ProjectManager::GetCurrentProject();
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
					BoltProject* project = ProjectManager::GetCurrentProject();
					if (project) {
						m_BuildOutputDir = Path::Combine(project->RootDirectory, "Builds", "Windows");
						std::snprintf(m_BuildOutputDirBuffer, sizeof(m_BuildOutputDirBuffer), "%s", m_BuildOutputDir.c_str());
					}
				}
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

		ImGui::EndMenuBar();
	}

	static bool IconButton(const char* id, const char* iconName, float iconSize, const ImVec2& btnSize) {
		unsigned int tex = EditorIcons::Get(iconName, (int)iconSize);
		if (tex)
			return ImGui::ImageButton(id, (ImTextureID)(intptr_t)tex, btnSize, ImVec2(0, 1), ImVec2(1, 0));
		return ImGui::Button(id + 2);
	}

	void ImGuiEditorLayer::RenderToolbar() {
		ImGui::Begin("Toolbar");

		const float iconSize = ImGui::GetTextLineHeight();
		const ImVec2 btnSize(iconSize, iconSize);
		const bool isPlaying = Application::GetIsPlaying();
		const bool isPaused = Application::IsPlaymodePaused();

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));

		if (!isPlaying) {
			if (IconButton("##Play", "play", iconSize, btnSize)) {
				Scene* active = SceneManager::Get().GetActiveScene();
				BoltProject* project = ProjectManager::GetCurrentProject();
				if (active && project) {
					std::string scenePath = project->GetSceneFilePath(active->GetName());
					if (active->IsDirty()) {
						SceneSerializer::SaveToFile(*active, scenePath);
						project->LastOpenedScene = active->GetName();
						project->Save();
					}
					m_PlayModeScenePath = scenePath;
				}
				m_LogEntries.clear();
				Application::SetPlaymodePaused(false);
				Application::SetIsPlaying(true);

				if (active) {
					StartPlayOnAwakeComponents(*active);
				}
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play (Enter playmode)");
		}
		else {
			if (isPaused) {
				if (IconButton("##Continue", "play", iconSize, btnSize)) {
					Application::SetPlaymodePaused(false);
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
					Application::SetPlaymodePaused(true);
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
				Application::SetPlaymodePaused(false);
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
		ImGui::End();

		// Handle step-frame logic: pause again after stepping one frame
		if (isPlaying && m_StepFrames > 0) {
			m_StepFrames--;
			if (m_StepFrames == 0)
				Application::SetPlaymodePaused(true);
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
		m_RenamingEntity = entt::null;
		m_EntityOrder.clear();

		Application::SetPlaymodePaused(false);
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

	void ImGuiEditorLayer::SelectEntity(EntityHandle entity) {
		m_SelectedEntity = entity;
		if (entity != entt::null) {
			m_AssetBrowser.ClearSelection();
		}
	}

	void ImGuiEditorLayer::ClearEntitySelection() {
		m_SelectedEntity = entt::null;
		m_RenamingEntity = entt::null;
		m_EntityRenameFrameCounter = 0;
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
		if (m_SelectedEntity == entt::null || !scene.IsValid(m_SelectedEntity)) {
			return;
		}

		Entity source = scene.GetEntity(m_SelectedEntity);
		Entity clone = scene.CreateEntity(source.GetName() + " (Clone)");

		const auto& componentRegistry = SceneManager::Get().GetComponentRegistry();
		componentRegistry.ForEachComponentInfo([&](const std::type_index&, const ComponentInfo& info) {
			if (info.category != ComponentCategory::Component) return;
			if (!info.has(source)) return;
			if (info.copyTo) {
				info.copyTo(source, clone);
			}
		});

		SelectEntity(clone.GetHandle());
		scene.MarkDirty();
		m_EntityOrder.clear();
	}

	void ImGuiEditorLayer::DeleteSelectedEntity(Scene& scene) {
		if (m_SelectedEntity == entt::null || !scene.IsValid(m_SelectedEntity)) {
			return;
		}

		scene.DestroyEntity(m_SelectedEntity);
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

				for (int entityIdx = 0; entityIdx < static_cast<int>(m_EntityOrder.size()); entityIdx++) {
					const EntityHandle entityHandle = m_EntityOrder[entityIdx];
					if (!scene.IsValid(entityHandle)) continue;
					Entity entity = scene.GetEntity(entityHandle);
					const bool selected = m_SelectedEntity == entityHandle;

					ImGui::PushID(static_cast<int>(static_cast<uint32_t>(entityHandle)));

					bool entityIsDisabled = scene.HasComponent<DisabledTag>(entityHandle);
					if (entityIsDisabled)
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));

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
						if (ImGui::Selectable(entityLabel.c_str(), selected)) {
							SelectEntity(entityHandle);
						}
						if (entityLabelTruncated && ImGui::IsItemHovered()) {
							ImGui::SetTooltip("%s", entity.GetName().c_str());
						}

						if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
							m_RenamingEntity = entityHandle;
							m_EntityRenameFrameCounter = 0;
							std::snprintf(m_EntityRenameBuffer, sizeof(m_EntityRenameBuffer), "%s", entity.GetName().c_str());
						}
					}

					// Drag-drop source: drag entity to reorder or to asset browser for prefab
					if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
						struct HierarchyDragData { int Index; uint32_t EntityHandle; };
						HierarchyDragData dragData{ entityIdx, static_cast<uint32_t>(entityHandle) };
						ImGui::SetDragDropPayload("HIERARCHY_ENTITY", &dragData, sizeof(dragData));
						ImGui::Text("Move: %s", entity.GetName().c_str());
						ImGui::EndDragDropSource();
					}

					// Drag-drop target: reorder entities, or accept .prefab from asset browser
					if (ImGui::BeginDragDropTarget()) {
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
							struct HierarchyDragData { int Index; uint32_t EntityHandle; };
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
						SelectEntity(entityHandle);

						if (ImGui::MenuItem("Delete Entity"))
						{
							scene.DestroyEntity(entity);
							if (m_SelectedEntity == entityHandle)
								ClearEntitySelection();
							if (m_RenamingEntity == entityHandle)
								m_RenamingEntity = entt::null;
							m_EntityOrder.clear();
						}

						if (ImGui::MenuItem("Duplicate"))
						{
							DuplicateSelectedEntity(scene);
						}

						if (ImGui::MenuItem("Rename"))
						{
							BeginRenameSelectedEntity(scene);
						}

						ImGui::EndPopup();
					}

					if (entityIsDisabled)
						ImGui::PopStyleColor();

					ImGui::PopID();
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
			&& ImGui::IsMouseClicked(ImGuiMouseButton_Left)
			&& !ImGui::IsAnyItemHovered()) {
			ClearEntitySelection();
		}

		m_IsEntitiesPanelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		ImGui::End();
	}

	void ImGuiEditorLayer::RenderInspectorPanel(Scene& scene) {
		ImGui::Begin("Inspector");

		if (m_SelectedEntity == entt::null || !scene.IsValid(m_SelectedEntity)) {
			m_SelectedEntity = entt::null;
			RenderAssetInspector();
			m_IsInspectorPanelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
			ImGui::End();
			m_InspectorItemWasActive = false;
			return;
		}

		Entity entity = scene.GetEntity(m_SelectedEntity);

		// ── Entity Header: Name + Toggles + UUID ──────────────────

		// Editable Name
		std::string entityName = entity.HasComponent<NameComponent>()
			? entity.GetComponent<NameComponent>().Name : "Entity";
		char nameBuf[256];
		std::snprintf(nameBuf, sizeof(nameBuf), "%s", entityName.c_str());
		ImGui::SetNextItemWidth(-1);
		if (ImGui::InputText("##EntityName", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
			if (entity.HasComponent<NameComponent>()) {
				entity.GetComponent<NameComponent>().Name = nameBuf;
			} else {
				scene.AddComponent<NameComponent>(m_SelectedEntity, std::string(nameBuf));
			}
			scene.MarkDirty();
		}

		// Toggles row: Enabled + Static
		{
			bool isDisabled = entity.HasComponent<DisabledTag>();
			bool isEnabled = !isDisabled;
			if (ImGui::Checkbox("Enabled", &isEnabled)) {
				if (isEnabled && entity.HasComponent<DisabledTag>())
					entity.RemoveComponent<DisabledTag>();
				else if (!isEnabled && !entity.HasComponent<DisabledTag>())
					entity.AddComponent<DisabledTag>();
				scene.MarkDirty();
			}

			ImGui::SameLine();

			bool isStatic = entity.HasComponent<StaticTag>();
			if (ImGui::Checkbox("Static", &isStatic)) {
				if (isStatic && !entity.HasComponent<StaticTag>())
					entity.AddComponent<StaticTag>();
				else if (!isStatic && entity.HasComponent<StaticTag>())
					entity.RemoveComponent<StaticTag>();
				scene.MarkDirty();
			}
		}

		// UUID display (read-only, small gray text)
		if (entity.HasComponent<UUIDComponent>()) {
			uint64_t uuid = static_cast<uint64_t>(entity.GetComponent<UUIDComponent>().Id);
			ImGui::TextDisabled("UUID: %llu", uuid);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Click to copy");
			}
			if (ImGui::IsItemClicked()) {
				ImGui::SetClipboardText(std::to_string(uuid).c_str());
			}
		}

		m_IsInspectorPanelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		ImGui::Separator();

		const auto& registry = SceneManager::Get().GetComponentRegistry();

		std::type_index pendingRemoval = typeid(void);

		registry.ForEachComponentInfo([&](const std::type_index& typeId, const ComponentInfo& info) {
			if (info.category != ComponentCategory::Component) return;
			if (info.displayName == "Name") return; // Shown in entity header
			if (!info.has(entity)) return;

			// Scripts render their own per-script sections — skip the outer wrapper
			if (info.displayName == "Scripts") {
				if (info.drawInspector) info.drawInspector(entity);
				return;
			}

			bool removeRequested = false;
			bool open = ImGuiUtils::BeginComponentSection(info.displayName.c_str(), removeRequested);

			// Component drag source on the header (attaches to the CollapsingHeader item)
			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
				uint64_t entityUUID = 0;
				if (entity.HasComponent<UUIDComponent>())
					entityUUID = static_cast<uint64_t>(entity.GetComponent<UUIDComponent>().Id);
				std::string refStr = std::to_string(entityUUID) + ":" + info.displayName;
				ImGui::SetDragDropPayload("COMPONENT_REF", refStr.c_str(), refStr.size() + 1);
				ImGui::Text("Ref: %s.%s", entity.GetName().c_str(), info.displayName.c_str());
				ImGui::EndDragDropSource();
			}

			if (removeRequested) {
				pendingRemoval = typeId;
			}

			if (open) {
				if (info.drawInspector) {
					info.drawInspector(entity);
				}
				ImGuiUtils::EndComponentSection();
			}
		});

		if (pendingRemoval != typeid(void)) {
			registry.ForEachComponentInfo([&](const std::type_index& typeId, const ComponentInfo& info) {
				if (typeId == pendingRemoval && info.remove) {
					info.remove(entity);
				}
			});
			scene.MarkDirty();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		float buttonWidth = ImGui::GetContentRegionAvail().x;
		if (ImGui::Button("Add Component", ImVec2(buttonWidth, 0))) {
			ImGui::OpenPopup("AddComponentPopup");
			m_ComponentSearchBuffer[0] = '\0';
		}

		if (ImGui::BeginPopup("AddComponentPopup")) {
			ImGui::SetNextItemWidth(-1);
			ImGui::InputTextWithHint("##CompSearch", "Search components...",
				m_ComponentSearchBuffer, sizeof(m_ComponentSearchBuffer));
			ImGui::Separator();

			std::string filter(m_ComponentSearchBuffer);
			std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

			bool hasFilter = !filter.empty();

			if (hasFilter) {
				// Flat filtered list when searching
				registry.ForEachComponentInfo([&](const std::type_index&, const ComponentInfo& info) {
					if (info.category != ComponentCategory::Component) return;
					if (info.has(entity)) return;

					std::string lowerName = info.displayName;
					std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
					if (lowerName.find(filter) == std::string::npos) return;

					if (ImGuiUtils::MenuItemEllipsis(info.displayName, info.displayName.c_str(), nullptr, false, true, 260.0f)) {
						info.add(entity);
						scene.MarkDirty();
					}
				});

				// Also search scripts by name
				{
					std::vector<std::pair<std::string, std::string>> scriptFiles; // className, path
					BoltProject* project = ProjectManager::GetCurrentProject();
					if (project) {
						std::filesystem::path scriptsDir(project->ScriptsDirectory);
						if (std::filesystem::exists(scriptsDir)) {
							for (const auto& entry : std::filesystem::recursive_directory_iterator(scriptsDir)) {
								if (!entry.is_regular_file()) continue;
								std::string ext = entry.path().extension().string();
								std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
								if (ext != ".cs") continue;
								std::string className = entry.path().stem().string();
								std::string lowerClassName = className;
								std::transform(lowerClassName.begin(), lowerClassName.end(), lowerClassName.begin(), ::tolower);
								if (lowerClassName.find(filter) != std::string::npos) {
									scriptFiles.emplace_back(className, entry.path().string());
								}
							}
						}
					}
					for (const auto& [className, path] : scriptFiles) {
						std::string label = className + "  .cs";
						if (ImGuiUtils::MenuItemEllipsis(label, path.c_str(), nullptr, false, true, 260.0f)) {
							if (!entity.HasComponent<ScriptComponent>()) {
								entity.AddComponent<ScriptComponent>();
							}
							auto& sc = entity.GetComponent<ScriptComponent>();
							if (!sc.HasScript(className)) {
								sc.AddScript(className);
							}
							scene.MarkDirty();
						}
					}
				}
			} else {
				// Categorized tree view
				// Collect components grouped by subcategory
				std::vector<std::pair<std::string, std::vector<const ComponentInfo*>>> categories;
				std::unordered_map<std::string, size_t> categoryIndex;

				// Define subcategory ordering
				const std::vector<std::string> subcategoryOrder = {
					"General", "Rendering", "Physics", "Audio"
				};
				for (const auto& sub : subcategoryOrder) {
					categoryIndex[sub] = categories.size();
					categories.emplace_back(sub, std::vector<const ComponentInfo*>{});
				}

				registry.ForEachComponentInfo([&](const std::type_index&, const ComponentInfo& info) {
					if (info.category != ComponentCategory::Component) return;
					if (info.has(entity)) return;

					std::string sub = info.subcategory.empty() ? "General" : info.subcategory;
					auto it = categoryIndex.find(sub);
					if (it == categoryIndex.end()) {
						categoryIndex[sub] = categories.size();
						categories.emplace_back(sub, std::vector<const ComponentInfo*>{});
						it = categoryIndex.find(sub);
					}
					categories[it->second].second.push_back(&info);
				});

				for (const auto& [subcategory, components] : categories) {
					if (components.empty() && subcategory != "Scripting") continue;

					if (ImGui::TreeNode(subcategory.c_str())) {
						for (const auto* info : components) {
							if (ImGuiUtils::MenuItemEllipsis(info->displayName, info->displayName.c_str(), nullptr, false, true, 260.0f)) {
								info->add(entity);
								scene.MarkDirty();
							}
						}
						ImGui::TreePop();
					}
				}

				// Scripts subcategory: individual .cs files as addable items
				{
					std::vector<std::pair<std::string, std::string>> scriptFiles; // className, path
					BoltProject* project = ProjectManager::GetCurrentProject();
					if (project) {
						std::filesystem::path scriptsDir(project->ScriptsDirectory);
						if (std::filesystem::exists(scriptsDir)) {
							for (const auto& entry : std::filesystem::recursive_directory_iterator(scriptsDir)) {
								if (!entry.is_regular_file()) continue;
								std::string ext = entry.path().extension().string();
								std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
								if (ext != ".cs") continue;
								scriptFiles.emplace_back(entry.path().stem().string(), entry.path().string());
							}
						}
					}
					std::sort(scriptFiles.begin(), scriptFiles.end());

					if (!scriptFiles.empty()) {
						if (ImGui::TreeNode("Scripts")) {
							for (const auto& [className, path] : scriptFiles) {
								if (ImGuiUtils::MenuItemEllipsis(className, path.c_str(), nullptr, false, true, 260.0f)) {
									if (!entity.HasComponent<ScriptComponent>()) {
										entity.AddComponent<ScriptComponent>();
									}
									auto& sc = entity.GetComponent<ScriptComponent>();
									if (!sc.HasScript(className)) {
										sc.AddScript(className);
									}
									scene.MarkDirty();
								}
							}
							ImGui::TreePop();
						}
					}
				}
			}

			ImGui::EndPopup();
		}

		// Drag-drop target: accept .cs files dropped onto the Inspector panel
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
				std::string droppedPath(static_cast<const char*>(payload->Data));
				std::string ext = std::filesystem::path(droppedPath).extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

				if (ext == ".cs") {
					std::string className = std::filesystem::path(droppedPath).stem().string();
					if (!entity.HasComponent<ScriptComponent>()) {
						entity.AddComponent<ScriptComponent>();
					}
					auto& sc = entity.GetComponent<ScriptComponent>();
					if (!sc.HasScript(className)) {
						sc.AddScript(className);
					}
					scene.MarkDirty();
				}
			}
			ImGui::EndDragDropTarget();
		}

		// Detect inspector property changes: when a widget finishes being edited, mark dirty
		bool anyActive = ImGui::IsAnyItemActive() && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
		if (m_InspectorItemWasActive && !anyActive) {
			scene.MarkDirty();
		}
		m_InspectorItemWasActive = anyActive;
		m_IsInspectorPanelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		ImGui::End();
	}
}
