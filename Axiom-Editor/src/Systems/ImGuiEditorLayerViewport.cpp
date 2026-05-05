#include <pch.hpp>
#include "Systems/ImGuiEditorLayer.hpp"

#include <imgui.h>
#include <glad/glad.h>

#include "Components/Components.hpp"
#include "Core/Application.hpp"
#include "Core/Window.hpp"
#include "Diagnostics/StatsOverlay.hpp"
#include "Editor/ApplicationEditorAccess.hpp"
#include "Graphics/GizmoRenderer.hpp"
#include "Graphics/Gizmo.hpp"
#include "Graphics/Renderer2D.hpp"
#include "Graphics/TextureManager.hpp"
#include "Gui/EditorIcons.hpp"
#include "Math/Trigonometry.hpp"
#include "Math/VectorMath.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace Axiom {
	namespace {
		struct GameViewAspectPreset {
			const char* Label;
			float Aspect = 0.0f;
		};

		constexpr std::array<GameViewAspectPreset, 6> k_GameViewAspectPresets = {{
			{ "Free Aspect", 0.0f },
			{ "16:9", 16.0f / 9.0f },
			{ "21:9", 21.0f / 9.0f },
			{ "4:3", 4.0f / 3.0f },
			{ "3:2", 3.0f / 2.0f },
			{ "9:16", 9.0f / 16.0f }
		}};
	}

	void ImGuiEditorLayer::RenderSceneIntoFBO(ViewportFBO& fbo, Scene& scene,
		const glm::mat4& vp, const AABB& viewportAABB,
		bool withGizmos, bool sharedGizmosOnly, const Color& clearColor)
	{
		auto* app = Application::GetInstance();
		if (!app) return;
		auto* renderer = app->GetRenderer2D();
		if (!renderer) return;

		int w = fbo.ViewportSize.GetWidth();
		int h = fbo.ViewportSize.GetHeight();

		glBindFramebuffer(GL_FRAMEBUFFER, fbo.FramebufferId);
		glViewport(0, 0, w, h);
		glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		SceneManager::Get().ForeachLoadedScene([&](const Scene& s) {
			renderer->RenderSceneWithVP(s, vp, viewportAABB);
			});

		if (withGizmos && Gizmo::IsEnabled()) {
			const GizmoLayerMask layerMask = sharedGizmosOnly ? GizmoLayerMask::Shared : GizmoLayerMask::All;
			GizmoRenderer2D::RenderWithVP(vp, layerMask);
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		auto* window = Application::GetWindow();
		if (window) {
			glViewport(0, 0, window->GetWidth(), window->GetHeight());
		}
	}

	void ImGuiEditorLayer::DrawEditorComponentGizmos(Scene& scene) {
		if (m_SelectedEntity == entt::null || !scene.IsValid(m_SelectedEntity)
			|| !scene.HasComponent<Transform2DComponent>(m_SelectedEntity)) {
			return;
		}

		auto& transform = scene.GetComponent<Transform2DComponent>(m_SelectedEntity);
		const float rotationDegrees = transform.GetRotationDegrees();

		const Color previousColor = Gizmo::GetColor();
		const float previousLineWidth = Gizmo::GetLineWidth();
		const GizmoLayer previousLayer = Gizmo::GetLayer();

		Gizmo::SetLayer(GizmoLayer::EditorOnly);

		Gizmo::SetColor(Color(1.0f, 0.65f, 0.10f, 1.0f));
		Gizmo::SetLineWidth(2.0f);
		Gizmo::DrawSquare(transform.Position, transform.Scale, rotationDegrees);

		if (scene.HasComponent<Camera2DComponent>(m_SelectedEntity)) {
			auto& camera = scene.GetComponent<Camera2DComponent>(m_SelectedEntity);
			Gizmo::SetColor(Color::White());
			Gizmo::SetLineWidth(1.5f);
			Gizmo::DrawSquare(transform.Position, camera.WorldViewPort(), rotationDegrees);
		}

		if (scene.HasComponent<BoxCollider2DComponent>(m_SelectedEntity)) {
			auto& collider = scene.GetComponent<BoxCollider2DComponent>(m_SelectedEntity);
			if (collider.IsValid()) {
				const Vec2 center = transform.Position + Rotated(collider.GetCenter(), transform.Rotation);
				Gizmo::SetColor(Color(0.20f, 1.0f, 0.35f, 1.0f));
				Gizmo::SetLineWidth(2.0f);
				Gizmo::DrawSquare(center, collider.GetScale(), rotationDegrees);
			}
		}

		if (scene.HasComponent<FastBoxCollider2DComponent>(m_SelectedEntity)) {
			auto& collider = scene.GetComponent<FastBoxCollider2DComponent>(m_SelectedEntity);
			const Vec2 halfExtents = collider.GetHalfExtents();
			const Vec2 worldSize(
				std::abs(halfExtents.x * transform.Scale.x) * 2.0f,
				std::abs(halfExtents.y * transform.Scale.y) * 2.0f);
			Gizmo::SetColor(Color(0.10f, 0.85f, 0.85f, 1.0f));
			Gizmo::SetLineWidth(2.0f);
			Gizmo::DrawSquare(transform.Position, worldSize, rotationDegrees);
		}

		if (scene.HasComponent<FastCircleCollider2DComponent>(m_SelectedEntity)) {
			auto& collider = scene.GetComponent<FastCircleCollider2DComponent>(m_SelectedEntity);
			const float worldRadius = collider.GetRadius() * std::max(std::abs(transform.Scale.x), std::abs(transform.Scale.y));
			Gizmo::SetColor(Color(0.10f, 0.85f, 0.85f, 1.0f));
			Gizmo::SetLineWidth(2.0f);
			Gizmo::DrawCircle(transform.Position, worldRadius);
		}

		if (scene.HasComponent<ParticleSystem2DComponent>(m_SelectedEntity)) {
			auto& particleSystem = scene.GetComponent<ParticleSystem2DComponent>(m_SelectedEntity);
			Gizmo::SetColor(Color(1.0f, 0.20f, 0.75f, 1.0f));
			Gizmo::SetLineWidth(2.0f);

			std::visit([&](auto&& shape) {
				using T = std::decay_t<decltype(shape)>;
				if constexpr (std::is_same_v<T, ParticleSystem2DComponent::CircleParams>) {
					const float radius = shape.Radius * std::max(std::abs(transform.Scale.x), std::abs(transform.Scale.y));
					Gizmo::DrawCircle(transform.Position, radius);
				}
				else if constexpr (std::is_same_v<T, ParticleSystem2DComponent::SquareParams>) {
					const Vec2 size(
						std::abs(shape.HalfExtends.x * transform.Scale.x) * 2.0f,
						std::abs(shape.HalfExtends.y * transform.Scale.y) * 2.0f);
					Gizmo::DrawSquare(transform.Position, size, rotationDegrees);
				}
			}, particleSystem.Shape);

			Vec2 moveDirection = particleSystem.ParticleSettings.MoveDirection;
			if (LengthSquared(moveDirection) < 0.0001f) {
				moveDirection = Up();
			}
			moveDirection = Normalized(Rotated(moveDirection, transform.Rotation));
			const float indicatorLength = std::max(0.75f, std::max(std::abs(transform.Scale.x), std::abs(transform.Scale.y)));
			Gizmo::DrawLine(transform.Position, transform.Position + moveDirection * indicatorLength);
		}

		Gizmo::SetLayer(previousLayer);
		Gizmo::SetColor(previousColor);
		Gizmo::SetLineWidth(previousLineWidth);
	}

	void ImGuiEditorLayer::RenderEditorView(Scene& scene) {
		m_IsEditorViewActive = ImGui::Begin("Editor View");

		if (!m_IsEditorViewActive) {
			m_IsEditorViewHovered = false;
			m_IsEditorViewFocused = false;
			ImGui::End();
			return;
		}

		const ImVec2 viewportSize = ImGui::GetContentRegionAvail();
		const int fbW = static_cast<int>(viewportSize.x);
		const int fbH = static_cast<int>(viewportSize.y);

		if (fbW > 0 && fbH > 0) {

			EnsureFBO(m_EditorViewFBO, fbW, fbH);
			m_EditorCamera.SetViewportSize(fbW, fbH);

			if (m_EditorViewFBO.FramebufferId != 0) {

				auto* app = Application::GetInstance();
				if (app) {
					auto& input = app->GetInput();
					float dt = app->GetTime().GetDeltaTimeUnscaled();

					Vec2 mouseDelta = { 0.0f, 0.0f };
					if (m_IsEditorViewHovered && input.GetMouse(MouseButton::Middle)) {
						mouseDelta = input.GetMouseDelta();
					}
					float scroll = m_IsEditorViewHovered ? input.ScrollValue() : 0.0f;

					m_EditorCamera.Update(dt, m_IsEditorViewHovered, mouseDelta, scroll);
				}

				glm::mat4 vp = m_EditorCamera.GetViewProjectionMatrix();
				AABB viewAABB = m_EditorCamera.GetViewportAABB();
				Gizmo::SetViewportAABBOverride(viewAABB);
				DrawEditorComponentGizmos(scene);

				static const Color k_EditorClearColor(0.18f, 0.18f, 0.20f, 1.0f);
				RenderSceneIntoFBO(m_EditorViewFBO, scene, vp, viewAABB, true, false, k_EditorClearColor);
				Gizmo::ClearViewportAABBOverride();

				ImGui::Image(
					static_cast<ImTextureID>(static_cast<intptr_t>(m_EditorViewFBO.ColorTextureId)),
					viewportSize,
					ImVec2(0.0f, 1.0f),
					ImVec2(1.0f, 0.0f));

				ImVec2 imageTopLeft = ImGui::GetItemRectMin();

				const float iconSize = 24.0f;
				const float halfIcon = iconSize * 0.5f;
				auto camView = scene.GetRegistry().view<Camera2DComponent, Transform2DComponent>();
				for (auto [ent, cam, transform] : camView.each()) {
					glm::vec4 worldPos(transform.Position.x, transform.Position.y, 0.0f, 1.0f);
					glm::vec4 clipPos = vp * worldPos;
					if (clipPos.w == 0.0f) continue;

					float ndcX = clipPos.x / clipPos.w;
					float ndcY = clipPos.y / clipPos.w;
					float screenX = (ndcX * 0.5f + 0.5f) * viewportSize.x;
					float screenY = (1.0f - (ndcY * 0.5f + 0.5f)) * viewportSize.y;

					if (screenX < -halfIcon || screenX > viewportSize.x + halfIcon ||
						screenY < -halfIcon || screenY > viewportSize.y + halfIcon) {
						continue;
					}

					unsigned int camIcon = EditorIcons::Get("camera", 24);
					if (!camIcon) {
						continue;
					}

					ImVec2 iconPos(imageTopLeft.x + screenX - halfIcon, imageTopLeft.y + screenY - halfIcon);
					ImGui::GetWindowDrawList()->AddImage(
						static_cast<ImTextureID>(static_cast<intptr_t>(camIcon)),
						iconPos,
						ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
						ImVec2(0, 1), ImVec2(1, 0));
				}
			}
		}
		else {
			ImGui::TextDisabled("Editor View has no drawable area");
		}

		m_IsEditorViewHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
		m_IsEditorViewFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		ImGui::End();
	}

	void ImGuiEditorLayer::RenderGameView(Scene& scene) {
		m_IsGameViewActive = ImGui::Begin("Game View");

		if (!m_IsGameViewActive) {
			ImGui::End();
			m_IsGameViewFocused = false;
			m_IsGameViewHovered = false;
			ApplicationEditorAccess::SetGameInputEnabled(false);
			return;
		}

		const int aspectPresetIndex = std::clamp(m_GameViewAspectPresetIndex, 0, static_cast<int>(k_GameViewAspectPresets.size()) - 1);
		m_GameViewAspectPresetIndex = aspectPresetIndex;
		if (!m_GameViewAspectLoaded) {
			if (AxiomProject* project = ProjectManager::GetCurrentProject()) {
				for (int i = 0; i < static_cast<int>(k_GameViewAspectPresets.size()); ++i) {
					if (project->GameViewAspect == k_GameViewAspectPresets[i].Label) {
						m_GameViewAspectPresetIndex = i;
						break;
					}
				}
			}
			m_GameViewAspectLoaded = true;
		}
		if (!m_GameViewVsyncLoaded) {
			if (AxiomProject* project = ProjectManager::GetCurrentProject()) {
				m_GameViewVsync = project->GameViewVsync;
			}
			m_GameViewVsyncLoaded = true;
		}

		ImGui::SetNextItemWidth(140.0f);
		if (ImGui::BeginCombo("##GameViewAspect", k_GameViewAspectPresets[m_GameViewAspectPresetIndex].Label)) {
			for (int i = 0; i < static_cast<int>(k_GameViewAspectPresets.size()); ++i) {
				const bool selected = (i == m_GameViewAspectPresetIndex);
				if (ImGui::Selectable(k_GameViewAspectPresets[i].Label, selected)) {
					m_GameViewAspectPresetIndex = i;
					if (AxiomProject* project = ProjectManager::GetCurrentProject()) {
						project->GameViewAspect = k_GameViewAspectPresets[i].Label;
						project->Save();
					}
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		ImGui::SameLine();
		if (ImGui::Checkbox("VSync##GameView", &m_GameViewVsync)) {
			m_GameViewHasRendered = false;
			if (AxiomProject* project = ProjectManager::GetCurrentProject()) {
				project->GameViewVsync = m_GameViewVsync;
				project->Save();
			}
		}

		// "Stats" toggle. Lives next to VSync; flips m_ShowGameViewStats so
		// the overlay block at the bottom of this function decides whether
		// to draw. Buttons show a depressed appearance when their overlay
		// is active so the user can see at a glance which are on.
		ImGui::SameLine();
		{
			const bool active = m_ShowGameViewStats;
			if (active) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			}
			if (ImGui::Button("Stats##GameView")) {
				m_ShowGameViewStats = !m_ShowGameViewStats;
			}
			if (active) {
				ImGui::PopStyleColor();
			}
		}

		// "Logs" toggle. Sibling to Stats; the log overlay stacks below
		// the stats overlay when both are visible.
		ImGui::SameLine();
		{
			const bool active = m_ShowGameViewLogs;
			if (active) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			}
			if (ImGui::Button("Logs##GameView")) {
				m_ShowGameViewLogs = !m_ShowGameViewLogs;
			}
			if (active) {
				ImGui::PopStyleColor();
			}
		}

		ImGui::Separator();

		const ImVec2 viewportSize = ImGui::GetContentRegionAvail();
		const float targetAspect = k_GameViewAspectPresets[m_GameViewAspectPresetIndex].Aspect;

		ImVec2 renderSize = viewportSize;
		if (targetAspect > 0.0f && viewportSize.x > 0.0f && viewportSize.y > 0.0f) {
			const float availableAspect = viewportSize.x / viewportSize.y;
			if (availableAspect > targetAspect) {
				renderSize.x = viewportSize.y * targetAspect;
			}
			else {
				renderSize.y = viewportSize.x / targetAspect;
			}
		}

		const int fbW = std::max(1, static_cast<int>(std::round(renderSize.x)));
		const int fbH = std::max(1, static_cast<int>(std::round(renderSize.y)));

		if (viewportSize.x > 0.0f && viewportSize.y > 0.0f) {
			EnsureFBO(m_GameViewFBO, fbW, fbH);

			Camera2DComponent* gameCam = Camera2DComponent::Main();
			if (m_GameViewFBO.FramebufferId != 0 && gameCam && gameCam->IsValid()) {
				Viewport* savedViewport = gameCam->GetViewport();
				if (!savedViewport) {
					ImGui::TextDisabled("Main camera has no viewport");
					m_IsGameViewHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
					m_IsGameViewFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
					ApplicationEditorAccess::SetGameInputEnabled(m_IsGameViewFocused);
					ImGui::End();
					return;
				}
				const int savedW = savedViewport->GetWidth();
				const int savedH = savedViewport->GetHeight();

				// RAII guard: if RenderSceneIntoFBO throws, the camera viewport
				// is still restored before the exception unwinds out of ImGui::End.
				struct ViewportRestoreGuard {
					Viewport* vp;
					int w;
					int h;
					Camera2DComponent* cam;
					~ViewportRestoreGuard() {
						vp->SetSize(w, h);
						cam->UpdateViewport();
					}
				} guard{ savedViewport, savedW, savedH, gameCam };

				savedViewport->SetSize(fbW, fbH);
				gameCam->UpdateViewport();
				glm::mat4 vp = gameCam->GetViewProjectionMatrix();
				AABB viewAABB = gameCam->GetViewportAABB();
				const auto now = std::chrono::steady_clock::now();
				float targetFps = 0.0f;
				if (m_GameViewVsync) {
					if (auto* window = Application::GetWindow()) {
						const GLFWvidmode* videoMode = window->GetVideomode();
						targetFps = videoMode ? static_cast<float>(videoMode->refreshRate) : 60.0f;
					}
					else {
						targetFps = 60.0f;
					}
				}
				else {
					targetFps = std::max(Application::GetTargetFramerate(), 0.0f);
				}

				bool renderFrame = !m_GameViewHasRendered
					|| m_LastGameViewFbW != fbW
					|| m_LastGameViewFbH != fbH;
				if (!renderFrame && targetFps > 0.0f) {
					const auto frameDuration = std::chrono::duration<double>(1.0 / static_cast<double>(targetFps));
					renderFrame = now - m_LastGameViewRenderTime >= frameDuration;
				}
				else if (targetFps <= 0.0f) {
					renderFrame = true;
				}

				if (renderFrame) {
					RenderSceneIntoFBO(m_GameViewFBO, scene, vp, viewAABB, true, true, gameCam->GetClearColor());
					m_LastGameViewRenderTime = now;
					m_LastGameViewFbW = fbW;
					m_LastGameViewFbH = fbH;
					m_GameViewHasRendered = true;
				}

				// guard's destructor restores the viewport — explicit restore here
				// is no longer needed and would be a redundant double-set.

				ImGui::InvisibleButton("##GameViewCanvas", viewportSize);
				const ImVec2 canvasMin = ImGui::GetItemRectMin();
				const ImVec2 canvasMax = ImGui::GetItemRectMax();
				ImDrawList* drawList = ImGui::GetWindowDrawList();
				drawList->AddRectFilled(canvasMin, canvasMax, IM_COL32(0, 0, 0, 255));

				const ImVec2 imageMin(
					canvasMin.x + (viewportSize.x - renderSize.x) * 0.5f,
					canvasMin.y + (viewportSize.y - renderSize.y) * 0.5f);
				const ImVec2 imageMax(imageMin.x + renderSize.x, imageMin.y + renderSize.y);

				drawList->AddImage(
					static_cast<ImTextureID>(static_cast<intptr_t>(m_GameViewFBO.ColorTextureId)),
					imageMin,
					imageMax,
					ImVec2(0.0f, 1.0f),
					ImVec2(1.0f, 0.0f));
				drawList->AddRect(imageMin, imageMax, IM_COL32(255, 255, 255, 40));

				// Stats overlay — engine-level helper. The cached snapshot
				// refreshes at 30 Hz internally; we just feed the FBO size
				// and let it draw inside the rendered image rectangle.
				// Tracks rendered height so the log overlay below can stack.
				float statsRenderedHeight = 0.0f;
				if (m_ShowGameViewStats) {
					m_GameViewStatsOverlay.RefreshIfDue(fbW, fbH);
					statsRenderedHeight = m_GameViewStatsOverlay.RenderInRect(imageMin, imageMax);
				}

				// Log overlay — pinned same place as stats; offset down when
				// stats is also visible so the two don't overlap. Lazy-
				// constructed: subscribing to Log::OnLog at engine load is
				// safe (Log is initialized very early), but we mirror the
				// runtime's lazy-construction style for symmetry.
				if (m_ShowGameViewLogs) {
					if (!m_GameViewLogOverlay) {
						m_GameViewLogOverlay = std::make_unique<Axiom::Diagnostics::LogOverlay>();
					}
					const float logYOffset = statsRenderedHeight > 0.0f
						? statsRenderedHeight + 8.0f
						: 0.0f;
					m_GameViewLogOverlay->RenderInRect(imageMin, imageMax, logYOffset);
				}
			}
			else {
				ImGui::TextDisabled("No main camera in scene");
			}
		}
		else {
			ImGui::TextDisabled("Game View has no drawable area");
		}

		m_IsGameViewHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
		m_IsGameViewFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		ApplicationEditorAccess::SetGameInputEnabled(m_IsGameViewFocused);
		ImGui::End();
	}

} // namespace Axiom
