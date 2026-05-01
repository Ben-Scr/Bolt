#pragma once
#include "QuadMesh.hpp"
#include "SpriteShaderProgram.hpp"
#include "TextureHandle.hpp"
#include "Instance44.hpp"
#include "Collections/AABB.hpp"

#include <glm/glm.hpp>
#include <vector>
#include <functional>

namespace Axiom {
	class Scene;

	class Renderer2D {
	public:
		void Initialize();
		void BeginFrame();
		void EndFrame();
		void Shutdown();
		void SetEnabled(bool enabled) { m_IsEnabled = enabled; }
		bool IsEnabled() const { return m_IsEnabled; }
		bool IsInitialized() const { return m_IsInitialized; }

		void SetOutputTarget(unsigned int fboId, int width, int height) {
			m_OutputFboId = fboId;
			m_OutputWidth = width;
			m_OutputHeight = height;
		}

		void RenderScene(const Scene& scene);
		void RenderSceneWithVP(const Scene& scene, const glm::mat4& vp, const AABB& viewportAABB);

		void SetSkipBeginFrameRender(bool skip) { m_SkipBeginFrameRender = skip; }

		size_t GetRenderedInstancesCount() const { return m_RenderedInstancesCount; }
		size_t GetDrawCallsCount() const { return m_DrawCallsCount; }
		float GetRenderLoopDuration() const { return m_RenderLoopDuration; }

		using SceneProvider = std::function<void(const std::function<void(const Scene&)>&)>;
		void SetSceneProvider(SceneProvider provider) { m_SceneProvider = std::move(provider); }

	private:
		void RenderScenes();
		void CollectAndRenderInstances(const Scene& scene, const glm::mat4& vp, const AABB& viewportAABB);

		size_t m_RenderedInstancesCount = 0;
		size_t m_DrawCallsCount = 0;
		float m_RenderLoopDuration = 0.0f;
		bool m_IsInitialized = false;
		bool m_IsEnabled = true;
		bool m_SkipBeginFrameRender = false;

		std::vector<Instance44> m_Instances;

		unsigned int m_OutputFboId = 0;
		int m_OutputWidth = 0;
		int m_OutputHeight = 0;

		QuadMesh m_QuadMesh;
		SpriteShaderProgram m_SpriteShader;

		SceneProvider m_SceneProvider;
	};
}
