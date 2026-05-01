#pragma once
#include "Graphics/SpriteShaderProgram.hpp"
#include "Graphics/QuadMesh.hpp"

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
		bool m_IsInitialized = false;
	};
}