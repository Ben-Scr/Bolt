#include "pch.hpp"
#include "Renderer2D.hpp"

#include "Core/Application.hpp"
#include "Core/Window.hpp"
#include "Scene/SceneManager.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Tags.hpp"
#include <Utils/Timer.hpp>

#include "Scene/Scene.hpp"
#include "Graphics/TextureManager.hpp"

#include <glad/glad.h>

namespace Bolt {
	namespace {
		Camera2DComponent* ResolveClearCamera() {
			Application* app = Application::GetInstance();
			if (!app || !app->GetSceneManager()) {
				return nullptr;
			}

			Scene* activeScene = app->GetSceneManager()->GetActiveScene();
			return activeScene ? activeScene->GetMainCamera() : nullptr;
		}

		bool TextureHandleLess(const TextureHandle& a, const TextureHandle& b) {
			if (a.index != b.index) {
				return a.index < b.index;
			}

			return a.generation < b.generation;
		}

		TextureHandle ResolveRenderableTextureHandle(TextureHandle requestedTexture, TextureHandle fallbackTexture) {
			if (TextureManager::IsValid(requestedTexture)) {
				return requestedTexture;
			}

			return fallbackTexture;
		}

		Texture2D* ResolveRenderableTexture(TextureHandle requestedTexture, TextureHandle fallbackTexture, TextureHandle& resolvedHandle) {
			resolvedHandle = ResolveRenderableTextureHandle(requestedTexture, fallbackTexture);
			return TextureManager::GetTexture(resolvedHandle);
		}
	}

	void Renderer2D::Initialize() {
		m_QuadMesh.Initialize();
		m_SpriteShader.Initialize();
		m_Instances.reserve(512);

		if (!m_SpriteShader.IsValid()) {
			BT_CORE_ERROR_TAG("Renderer2D", "Sprite shader is invalid — rendering disabled");
			return;
		}
		m_IsInitialized = true;
	}

	void Renderer2D::BeginFrame() {
		Timer timer = Timer();

		if (m_SkipBeginFrameRender) {
			m_DrawCallsCount = 0;
			m_RenderLoopDuration = timer.ElapsedMilliseconds();
			return;
		}

		if (m_OutputFboId != 0) {
			const int savedW = Window::GetMainViewport()->GetWidth();
			const int savedH = Window::GetMainViewport()->GetHeight();

			glBindFramebuffer(GL_FRAMEBUFFER, m_OutputFboId);
			glViewport(0, 0, m_OutputWidth, m_OutputHeight);
			Window::GetMainViewport()->SetSize(m_OutputWidth, m_OutputHeight);

			Camera2DComponent* cam = ResolveClearCamera();
			if (cam) {
				const auto& cc = cam->GetClearColor();
				glClearColor(cc.r, cc.g, cc.b, cc.a);
			}
			glClear(GL_COLOR_BUFFER_BIT);
			if (m_IsInitialized && m_IsEnabled)
				RenderScenes();
			else {
				m_RenderedInstancesCount = 0;
				m_DrawCallsCount = 0;
			}

			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glViewport(0, 0, savedW, savedH);
			Window::GetMainViewport()->SetSize(savedW, savedH);
			m_OutputFboId = 0;
		}
		else {
			Camera2DComponent* cam = ResolveClearCamera();
			if (cam) {
				const auto& cc = cam->GetClearColor();
				glClearColor(cc.r, cc.g, cc.b, cc.a);
			}
			glClear(GL_COLOR_BUFFER_BIT);
			if (m_IsInitialized && m_IsEnabled)
				RenderScenes();
			else {
				m_RenderedInstancesCount = 0;
				m_DrawCallsCount = 0;
			}
		}

		m_RenderLoopDuration = timer.ElapsedMilliseconds();
	}

	void Renderer2D::EndFrame() {
		if (!m_IsEnabled) return;
	}

	void Renderer2D::RenderScenes() {
		if (m_SceneProvider) {
			m_SceneProvider([this](const Scene& scene) { RenderScene(scene); });
		}
		else {
			SceneManager::Get().ForeachLoadedScene([this](const Scene& scene) {
				RenderScene(scene);
			});
		}
	}

	void Renderer2D::RenderScene(const Scene& scene) {
		Camera2DComponent* camera2D = const_cast<Camera2DComponent*>(scene.GetMainCamera());
		if (!camera2D) {
			BT_WARN_TAG("Renderer2D", "No main camera found in the scene. Nothing will be rendered.");
			return;
		}

		camera2D->UpdateViewport();
		CollectAndRenderInstances(scene, camera2D->GetViewProjectionMatrix(), camera2D->GetViewportAABB());
	}

	void Renderer2D::RenderSceneWithVP(const Scene& scene, const glm::mat4& vp, const AABB& viewportAABB) {
		if (!m_IsInitialized || !m_IsEnabled) return;
		CollectAndRenderInstances(scene, vp, viewportAABB);
	}

	void Renderer2D::CollectAndRenderInstances(const Scene& scene, const glm::mat4& vp, const AABB& viewportAABB) {
		if (!m_SpriteShader.IsValid()) {
			BT_CORE_ERROR_TAG("Renderer2D", "Sprite shader is invalid — cannot render");
			return;
		}
		m_SpriteShader.Bind();
		m_SpriteShader.SetMVP(vp);

		m_Instances.clear();

		auto ptsView = scene.GetRegistry().view<ParticleSystem2DComponent>(entt::exclude<DisabledTag>);
		auto srView = scene.GetRegistry().view<Transform2DComponent, SpriteRendererComponent>(entt::exclude<DisabledTag>);

		m_Instances.reserve(ptsView.size_hint() + srView.size_hint());

		for (const auto& [ent, particleSystem] : ptsView.each()) {
			for (const auto& particle : particleSystem.GetParticles()) {
				if (!AABB::Intersects(viewportAABB, AABB::FromTransform(particle.Transform)))
					continue;

				m_Instances.emplace_back(
					particle.Transform.Position,
					particle.Transform.Scale,
					particle.Transform.Rotation,
					particle.Color,
					particleSystem.m_TextureHandle,
					particleSystem.RenderingSettings.SortingOrder,
					particleSystem.RenderingSettings.SortingLayer
				);
			}
		}

		for (const auto& [ent, tr, spriteRenderer] : srView.each()) {
			if (!AABB::Intersects(viewportAABB, AABB::FromTransform(tr)))
				continue;

			m_Instances.emplace_back(
				tr.Position,
				tr.Scale,
				tr.Rotation,
				spriteRenderer.Color,
				spriteRenderer.TextureHandle,
				spriteRenderer.SortingOrder,
				spriteRenderer.SortingLayer
			);
		}

		std::sort(m_Instances.begin(), m_Instances.end(),
			[](const Instance44& a, const Instance44& b) {
				if (a.SortingLayer != b.SortingLayer)
					return a.SortingLayer < b.SortingLayer;
				if (a.SortingOrder != b.SortingOrder)
					return a.SortingOrder < b.SortingOrder;
				if (a.TextureHandle != b.TextureHandle) {
					return TextureHandleLess(a.TextureHandle, b.TextureHandle);
				}

				return false;
			});

		m_QuadMesh.Bind();
		glActiveTexture(GL_TEXTURE0);

		const TextureHandle fallbackTexture = TextureManager::GetDefaultTexture(DefaultTexture::Square);
		m_DrawCallsCount = 0;

		for (size_t batchStart = 0; batchStart < m_Instances.size(); ) {
			TextureHandle batchTextureHandle{};
			Texture2D* batchTexture = ResolveRenderableTexture(
				m_Instances[batchStart].TextureHandle,
				fallbackTexture,
				batchTextureHandle);

			size_t batchEnd = batchStart + 1;
			for (; batchEnd < m_Instances.size(); ++batchEnd) {
				TextureHandle nextTextureHandle = ResolveRenderableTextureHandle(
					m_Instances[batchEnd].TextureHandle,
					fallbackTexture);
				if (!(nextTextureHandle == batchTextureHandle)) {
					break;
				}
			}

			if (batchTexture && batchTexture->IsValid()) {
				batchTexture->Submit(0);
			}
			else {
				glBindTexture(GL_TEXTURE_2D, 0);
			}

			m_QuadMesh.UploadInstances(std::span<const Instance44>(m_Instances.data() + batchStart, batchEnd - batchStart));
			m_QuadMesh.DrawInstanced(batchEnd - batchStart);
			++m_DrawCallsCount;
			batchStart = batchEnd;
		}

		m_QuadMesh.Unbind();
		m_SpriteShader.Unbind();
		m_RenderedInstancesCount = m_Instances.size();
	}

	void Renderer2D::Shutdown() {
		m_QuadMesh.Shutdown();
		m_SpriteShader.Shutdown();
		m_Instances.clear();
		m_Instances.shrink_to_fit();
		m_DrawCallsCount = 0;
	}
}
