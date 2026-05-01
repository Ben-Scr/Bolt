#pragma once

#include <GLFW/glfw3.h>

namespace Axiom {
	class Viewport;

	class ImGuiRenderer {
	public:
		ImGuiRenderer() = default;
		void Initialize(GLFWwindow* window);
		void Shutdown();

		void BeginFrame();
		void EndFrame();

	private:
		static void ApplyAxiomTheme();
		bool m_IsInitialized = false;
	};
}