#pragma once
#include "Graphics/SpriteShaderProgram.hpp"
#include "Graphics/QuadMesh.hpp"
#include "Graphics/Instance44.hpp"

#include <vector>

namespace Axiom {
	class Scene;
	class SceneManager;

	class GuiRenderer {
	public:
		GuiRenderer() = default;
		void Initialize();
		void Shutdown();

		void BeginFrame(const SceneManager& sceneManager);
		void EndFrame();

		void RenderScene(const Scene& scene);

	private:
		SpriteShaderProgram m_SpriteShader;
		QuadMesh m_QuadMesh;
		// Reused across frames so RenderScene doesn't heap-allocate per frame.
		std::vector<Instance44> m_InstancesScratch;
		bool m_IsInitialized = false;
	};
}