#include "pch.hpp"
#include "GuiRenderer.hpp"

#include "Scene/SceneManager.hpp"
#include "Scene/Scene.hpp"
#include "Graphics/TextureManager.hpp"

#include "Components/Graphics/ImageComponent.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#include <Components/Tags.hpp>

#include "Graphics/Shader.hpp"
#include "Graphics/Instance44.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace Axiom {
	void GuiRenderer::Initialize() {
		if (m_IsInitialized) {
			return;
		}

		m_SpriteShader.Initialize();
		m_QuadMesh.Initialize();

		m_IsInitialized = true;
	}
	void GuiRenderer::Shutdown() {
		if (!m_IsInitialized) {
			return;
		}

		m_QuadMesh.Shutdown();
		m_SpriteShader.Shutdown();
		m_IsInitialized = false;
	}

	void GuiRenderer::BeginFrame(const SceneManager& sceneManager) {
		if (!m_IsInitialized) {
			return;
		}

		sceneManager.ForeachLoadedScene([&](const Scene& scene) { RenderScene(scene); });
	}
	void GuiRenderer::EndFrame() {
		if (!m_IsInitialized) {
			return;
		}

	}
	void GuiRenderer::RenderScene(const Scene& scene) {
		if (!m_IsInitialized) {
			return;
		}

		AIM_ASSERT(m_SpriteShader.IsValid(), AxiomErrorCode::InvalidHandle, "Invalid Sprite 2D Shader");
		m_SpriteShader.Bind();

		const Camera2DComponent* camera2DConst = scene.GetMainCamera();
		if (!camera2DConst) {
			AIM_WARN_TAG("GuiRenderer", "Skipping GUI render for scene '{}' because it has no enabled main camera.", scene.GetName());
			m_SpriteShader.Unbind();
			return;
		}
		// Mutation of the camera's viewport state is unavoidable today (UpdateViewport
		// recomputes matrices). When the renderer is moved to a Camera-update-system
		// (TODO), this const_cast disappears.
		Camera2DComponent* camera2D = const_cast<Camera2DComponent*>(camera2DConst);

		camera2D->UpdateViewport();
		Viewport* vp = camera2D->GetViewport();
		if (!vp) {
			// Camera owns the viewport via Window::GetMainViewport(); a missing
			// viewport here means the window was already torn down. Skip silently
			// rather than null-deref.
			m_SpriteShader.Unbind();
			return;
		}

		const int w = vp->GetWidth();
		const int h = vp->GetHeight();
		const float halfW = 0.5f * w;
		const float halfH = 0.5f * h;
		const float zNear = -1.0f;
		const float zFar = 1.0f;

		m_SpriteShader.SetMVP(glm::ortho(-halfW, +halfW, -halfH, +halfH, zNear, zFar));

		m_InstancesScratch.clear();
		auto guiImageView = scene.GetRegistry().view<RectTransform2DComponent, ImageComponent>(entt::exclude<DisabledTag>);
		m_InstancesScratch.reserve(guiImageView.size_hint());

		for (const auto& [ent, rt, guiImage] : guiImageView.each()) {
			Vec2 bl = rt.GetBottomLeft();

			m_InstancesScratch.emplace_back(
				bl,
				rt.GetSize(),
				rt.Rotation,
				guiImage.Color,
				guiImage.TextureHandle,
				100,
				100
			);
		}

		std::sort(m_InstancesScratch.begin(), m_InstancesScratch.end(),
			[](const Instance44& a, const Instance44& b) {
				if (a.SortingLayer != b.SortingLayer)
					return a.SortingLayer < b.SortingLayer;
				return a.SortingOrder < b.SortingOrder;
			});

		const GLboolean wasScissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
		const GLboolean wasDepthEnabled = glIsEnabled(GL_DEPTH_TEST);
		const GLboolean wasBlendEnabled = glIsEnabled(GL_BLEND);
		GLint previousBlendSrcRgb = GL_ONE;
		GLint previousBlendDstRgb = GL_ZERO;
		GLint previousBlendSrcAlpha = GL_ONE;
		GLint previousBlendDstAlpha = GL_ZERO;
		GLint previousBlendEquationRgb = GL_FUNC_ADD;
		GLint previousBlendEquationAlpha = GL_FUNC_ADD;
		glGetIntegerv(GL_BLEND_SRC_RGB, &previousBlendSrcRgb);
		glGetIntegerv(GL_BLEND_DST_RGB, &previousBlendDstRgb);
		glGetIntegerv(GL_BLEND_SRC_ALPHA, &previousBlendSrcAlpha);
		glGetIntegerv(GL_BLEND_DST_ALPHA, &previousBlendDstAlpha);
		glGetIntegerv(GL_BLEND_EQUATION_RGB, &previousBlendEquationRgb);
		glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &previousBlendEquationAlpha);

		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// Bind once outside the loop — the previous code rebound the same QuadMesh
		// for each instance. Per-element bind/draw violates the engine's single-quad-
		// instanced design intent and ate a measurable chunk of GPU time at scale.
		// (Real instancing of GUI quads is a TODO; this just stops the per-call thrash.)
		m_QuadMesh.Bind();

		for (const Instance44& instance : m_InstancesScratch) {
			m_SpriteShader.SetSpritePosition(instance.Position);
			m_SpriteShader.SetScale(instance.Scale);
			m_SpriteShader.SetRotation(instance.Rotation);
			m_SpriteShader.SetUV(glm::vec2(0.0f), glm::vec2(1.0f));

			glActiveTexture(GL_TEXTURE0);
			TextureHandle handle = instance.TextureHandle;
			if (!handle.IsValid()) {
				handle = TextureManager::GetDefaultTexture(DefaultTexture::Square);
			}
			Texture2D* texture = TextureManager::GetTexture(handle);
			if (texture && texture->IsValid())
				texture->Submit(0);

			m_SpriteShader.SetVertexColor(instance.Color);
			m_QuadMesh.Draw();
		}

		m_QuadMesh.Unbind();
		m_SpriteShader.Unbind();

		glBlendEquationSeparate(previousBlendEquationRgb, previousBlendEquationAlpha);
		glBlendFuncSeparate(previousBlendSrcRgb, previousBlendDstRgb, previousBlendSrcAlpha, previousBlendDstAlpha);
		wasScissorEnabled ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
		wasDepthEnabled ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
		wasBlendEnabled ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
	}
}
