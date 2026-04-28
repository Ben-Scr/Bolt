#include <pch.hpp>
#include "ImGuiDebugSystem.hpp"

#include <imgui.h>

#include "Components/Components.hpp"
#include "Core/Application.hpp"
#include "Core/Memory.hpp"
#include "Core/Time.hpp"
#include "Core/Window.hpp"
#include "Graphics/Gizmo.hpp"
#include "Graphics/OpenGL.hpp"
#include "Graphics/Renderer2D.hpp"
#include "Scene/EntityHelper.hpp"
#include "Scene/SceneManager.hpp"
#include <Utils/StringHelper.hpp>

#include <Core/Version.hpp>

namespace Bolt {

	void ImGuiDebugSystem::OnImGuiRender(Application& app) {
		auto* window = app.GetWindow();
		auto* renderer2D = app.GetRenderer2D();
		if (!window || !renderer2D) return;

		Scene* activeScene = SceneManager::Get().GetActiveScene();

		ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_MenuBar);

		if (ImGui::BeginMenuBar()) {
			if (ImGui::BeginMenu("Application")) {
				if (ImGui::MenuItem("Reload App")) {
					Application::Reload();
				}

				if (ImGui::MenuItem("Quit")) {
					Application::RequestQuit();
				}

				ImGui::EndMenu();
			}

			ImGui::EndMenuBar();
		}

		if (ImGui::CollapsingHeader("Display & Graphics", ImGuiTreeNodeFlags_DefaultOpen)) {
			bool isFullscreen = window->IsFullScreen();
			if (ImGui::Checkbox("Fullscreen", &isFullscreen)) {
				window->SetFullScreen(isFullscreen);
			}

			bool isDecorated = window->IsDecorated();
			if (ImGui::Checkbox("Decorated", &isDecorated)) {
				window->SetDecorated(isDecorated);
			}

			bool isResizeable = window->IsResizeable();
			if (ImGui::Checkbox("Resizeable", &isResizeable)) {
				window->SetResizeable(isResizeable);
			}

			bool isVsync = window->IsVsync();
			if (ImGui::Checkbox("Vsync", &isVsync)) {
				window->SetVsync(isVsync);
			}

			if (!isVsync) {
				float targetFrameRate = Application::GetTargetFramerate();
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);

				if (ImGui::SliderFloat("Target FPS", &targetFrameRate, 30.f, 244.f, "%.0f FPS")) {
					Application::SetTargetFramerate(targetFrameRate);
				}
			}

			ImGui::Separator();

			std::array<float, 4> bgCol = OpenGL::GetClearColor().ToArray();
			if (ImGui::ColorEdit4("Clear Color", bgCol.data(), ImGuiColorEditFlags_NoInputs)) {
				OpenGL::SetClearColor(Color::FromArray(bgCol));
			}

			if (ImGui::CollapsingHeader("Time & Simulation")) {
				auto& time = Application::GetInstance()->GetTime();

				float timeScale = time.GetTimeScale();
				if (ImGui::SliderFloat("Timescale", &timeScale, 0.f, 10.f, "%.2fx")) {
					time.SetTimeScale(timeScale);
				}

				float fixedFPS = 1.f / time.GetUnscaledFixedDeltaTime();
				if (ImGui::SliderFloat("Fixed Update (Hz)", &fixedFPS, 10.f, 244.f, "%.0f Hz")) {
					time.SetFixedDeltaTime(1.f / fixedFPS);
				}

				bool runInBG = Application::GetRunInBackground();
				if (ImGui::Checkbox("Run in Background", &runInBG)) {
					Application::SetRunInBackground(runInBG);
				}
			}

			if (ImGui::CollapsingHeader("Gizmos & Debug Draw")) {
				bool renderer2DEnabled = renderer2D->IsEnabled();
				if (ImGui::Checkbox("Enable Renderer2D", &renderer2DEnabled)) {
					renderer2D->SetEnabled(renderer2DEnabled);

					ImGui::Separator();
				}

				bool enabledGizmo = Gizmo::IsEnabled();
				if (ImGui::Checkbox("Show Gizmos", &enabledGizmo)) {
					Gizmo::SetEnabled(enabledGizmo);
				}

				if (enabledGizmo) {
					ImGui::Indent();

					static bool aabb = true;
					ImGui::Checkbox("AABB Boxes", &aabb);

					ImGui::SameLine();

					static bool collider = true;
					ImGui::Checkbox("Colliders", &collider);

					float lineWidth = Gizmo::GetLineWidth();
					if (ImGui::SliderFloat("Line Width", &lineWidth, 1.f, 5.f)) {
						Gizmo::SetLineWidth(lineWidth);
					}

					std::array<float, 4> gizmoCol = Gizmo::GetColor().ToArray();
					if (ImGui::ColorEdit4("Gizmo Tint", gizmoCol.data(), ImGuiColorEditFlags_NoInputs)) {
						Gizmo::SetColor(Color::FromArray(gizmoCol));
					}

					ImGui::Unindent();
				}

				ImGui::Spacing();
				ImGui::Separator();

				if (ImGui::Button("Reload Scene", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
					if (activeScene) {
						SceneManager::Get().ReloadScene(activeScene->GetName());
					}
					ImGui::End();
					return;
				}
			}
		}

		ImGui::End();

		ImGui::Begin("Debug Info", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
		ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), BT_VERSION_LONG);
		ImGui::Separator();

		if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
			auto& time = Application::GetInstance()->GetTime();
			float fpsValue = 1.f / time.GetDeltaTimeUnscaled();
			ImVec4 fpsColor =
				fpsValue < 30.0f
				? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
				: ImVec4(0.3f, 1.0f, 0.3f, 1.0f);

			ImGui::Text("FPS: ");
			ImGui::SameLine();
			ImGui::TextColored(fpsColor, "%.2f", fpsValue);

			ImGui::Text("Frame Count: %d", time.GetFrameCount());
			ImGui::Text("Time Scale:  %.2f", time.GetTimeScale());

			if (ImGui::CollapsingHeader("Memory")) {
				auto stats = Memory::GetAllocationStats();
				size_t activeMem = stats.TotalAllocated - stats.TotalFreed;

				if (ImGui::BeginTable("MemoryTable", 2)) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Total Allocated:");

					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(StringHelper::ToIEC(stats.TotalAllocated).c_str());

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Active Usage:");

					ImGui::TableSetColumnIndex(1);
					ImGui::TextColored(
						ImVec4(0.4f, 0.7f, 1.0f, 1.0f),
						"%s",
						StringHelper::ToIEC(activeMem).c_str()
					);

					ImGui::EndTable();
				}
			}

			if (ImGui::CollapsingHeader("Scene Manager")) {
				auto& sceneManager = SceneManager::Get();

				Scene* activeScene = sceneManager.GetActiveScene();
				const std::string activeSceneLabel = activeScene
					? ("Active Scene: " + activeScene->GetName())
					: "Active Scene: <none>";
				ImGui::Text("%s", activeSceneLabel.c_str());
				ImGui::Text("Registered Scenes");

				for (const auto& registeredScene : sceneManager.GetRegisteredSceneNames()) {
					ImGui::BulletText("%s", registeredScene.c_str());
				}

				ImGui::Text("Loaded Scenes");

				sceneManager.ForeachLoadedScene([](const Scene& scene) {
					ImGui::BulletText("%s", scene.GetName().c_str());
					});
			}

			if (ImGui::CollapsingHeader("Renderer")) {
				ImGui::BulletText("Instances: %d", renderer2D->GetRenderedInstancesCount());
				ImGui::BulletText("Loop Duration: %.3f ms", renderer2D->GetRenderLoopDuration());

				ImGui::Spacing();
				ImGui::TextDisabled("Window & Viewport:");
				ImGui::Indent();
				ImGui::Text("Size: %d x %d", window->GetWidth(), window->GetHeight());
				ImGui::Text("VP:   %s", StringHelper::ToString(*window->GetMainViewport()).c_str());
				ImGui::Unindent();
			}
		}

		ImGui::End();
	}

}
