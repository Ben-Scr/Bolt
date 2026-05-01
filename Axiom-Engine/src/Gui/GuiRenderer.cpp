#include "pch.hpp"
#include "GuiRenderer.hpp"

#include "Scene/SceneManager.hpp"
#include "Scene/Scene.hpp"
#include "Graphics/TextureManager.hpp"

#include "Components/Graphics/ImageComponent.hpp"
#include "Components/General/RectTransformComponent.hpp"
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

		Camera2DComponent* camera2D = const_cast<Camera2DComponent*>(scene.GetMainCamera());
		if (!camera2D) {
			AIM_WARN_TAG("GuiRenderer", "Skipping GUI render for scene '{}' because it has no enabled main camera.", scene.GetName());
			m_SpriteShader.Unbind();
			return;
		}

		camera2D->UpdateViewport();
		Viewport* vp = camera2D->GetViewport();


		int w = vp->GetWidth();
		int h = vp->GetHeight();

		float halfW = 0.5f * w;
		float halfH = 0.5f * h;

		float zNear = -1.0f;
		float zFar = 1.0f;

		m_SpriteShader.SetMVP(glm::ortho(-halfW, +halfW, -halfH, +halfH, zNear, zFar));
		std::vector<Instance44> instances;


		auto guiImageView = scene.GetRegistry().view<RectTransformComponent, ImageComponent>(entt::exclude<DisabledTag>);
		instances.reserve(guiImageView.size_hint());

		for (const auto& [ent, rt, guiImage] : guiImageView.each()) {
			Vec2 bl = rt.GetBottomLeft();

			instances.emplace_back(
				bl,
				rt.GetSize(),
				rt.Rotation,
				guiImage.Color,
				guiImage.TextureHandle,
				100,
				100
			);
		}

		std::sort(instances.begin(), instances.end(),
			[](const Instance44& a, const Instance44& b) {
				if (a.SortingLayer != b.SortingLayer)
					return a.SortingLayer < b.SortingLayer;
				return a.SortingOrder < b.SortingOrder;
			});

		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// (Ben-Scr) Final Rendering
		for (const Instance44& instance : instances) {
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


			m_QuadMesh.Bind();
			m_SpriteShader.SetVertexColor(instance.Color);
			m_QuadMesh.Draw();
			m_QuadMesh.Unbind();
		}

		m_SpriteShader.Unbind();
	}
}
