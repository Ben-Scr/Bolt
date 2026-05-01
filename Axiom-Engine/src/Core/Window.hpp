#pragma once
#include "Collections/Vec2.hpp"
#include "Collections/Viewport.hpp"
#include "Core/Export.hpp"
#include "Core/WindowSpecification.hpp"
#include "Events/AxiomEvent.hpp"

#include <GLFW/glfw3.h>
#include <string>
#include <memory>
#include <functional>

namespace Axiom {
	class Texture2D;

	class AXIOM_API Window {
		friend class Application;

	public:
		Window(int width, int height, const std::string& title);
		Window(const WindowSpecification& props);

		// Info: Initializes the GLFW library
		static void Initialize();
		// Info: Terminates the GLFW library
		static void Shutdown();
		static bool IsInitialized() { return s_IsInitialized; }

		static void SetVsync(bool enabled) { glfwSwapInterval(enabled); s_IsVsync = enabled; };

		void Destroy();

		void SetClipboardString(const std::string& s) { glfwSetClipboardString(m_GLFWwindow, s.c_str()); }
		void SetWindowResizeable(bool enabled) { glfwSetWindowAttrib(m_GLFWwindow, GLFW_RESIZABLE, enabled ? GLFW_TRUE : GLFW_FALSE); }
		void SetWindowMoveable(bool enabled) { glfwSetWindowAttrib(m_GLFWwindow, GLFW_DECORATED, enabled ? GLFW_TRUE : GLFW_FALSE); }
		void SetPosition(Vec2Int pos) { glfwSetWindowPos(m_GLFWwindow, pos.x, pos.y); }
		void SetSize(Vec2Int size) { glfwSetWindowSize(m_GLFWwindow, size.x, size.y); }
		void SetCursorPosition(Vec2 position) { glfwSetCursorPos(m_GLFWwindow, (double)position.x, (double)position.y); }

		void SetCursorLocked(bool enabled);
		void SetCursorHidden(bool enabled);


		void SetCursorImage(const Texture2D* tex2D);
		void SetWindowIcon(const Texture2D* tex2D);
		void SetWindowIconFromResource();

		// Info: If enabled equals true the window will become fullscreen else windowed
		void SetFullScreen(bool enabled);
		void SetDecorated(bool enabled);
		void SetVisible(bool enabled);
		void SetResizeable(bool enabled);
		void SetTitle(const std::string& title) { glfwSetWindowTitle(m_GLFWwindow, title.c_str()); }

		std::string GetTitle() const { return std::string(glfwGetWindowTitle(m_GLFWwindow)); }
		int GetWidth()  const { return s_MainViewport->GetWidth(); }
		int GetHeight() const { return s_MainViewport->GetHeight(); }
		GLFWmonitor* GetWindowMonitor() const;
		static GLFWmonitor* GetMainMonitor();
		const GLFWvidmode* GetVideomode() const { return k_Videomode; }
		GLFWwindow* GetGLFWWindow() const { return m_GLFWwindow; }
		float GetAspect() const { return s_MainViewport->GetAspect(); }
		Vec2Int GetSize() const { return s_MainViewport->GetSize(); }
		Vec2Int GetPosition() const {
			Vec2Int pos;
			glfwGetWindowPos(m_GLFWwindow, &pos.x, &pos.y);
			return pos;
		}

		std::string GetClipboardString() const;
		Vec2 GetCursorPosition() const;


		bool IsMaximized() const;
		bool IsMinimized() const;
		bool IsFullScreen() const;
		bool IsVisible() const;
		bool IsDecorated() const;
		bool IsResizeable() const;

		static bool IsVsync() { return s_IsVsync; }

		// Info: If reset equals true the window will automatically be restored if it's already maximized
		void MaximizeWindow(bool reset = false);
		void MinimizeWindow();
		void RestoreWindow();

		void CenterWindow();
		void Focus();

		void SetEventCallback(const EventCallbackFn& callback) { m_EventCallback = callback; }

		static Window* GetActiveWindow() { return s_ActiveWindow; }
		static Viewport* GetMainViewport() { return s_MainViewport.get(); }

	private:
		void Create(const WindowSpecification& props);
		void SyncViewportFromFramebuffer();
		void UpdateViewport();
		void SwapBuffers() const { glfwSwapBuffers(m_GLFWwindow); }

		bool ShouldClose() const { return glfwWindowShouldClose(m_GLFWwindow); }
		Vec2Int GetScreenCenter() const;
		Vec2 GetWindowCenter() const;

		static void FocusCallback(GLFWwindow* window, int focused);
		static void RefreshCallback(GLFWwindow* window);
		static void SetKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
		static void SetMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
		static void SetCursorPositionCallback(GLFWwindow* window, double xPos, double yPos);
		static void SetScrollCallback(GLFWwindow* window, double xOffset, double yOffset);
		static void SetWindowResizedCallback(GLFWwindow* window, int width, int height);
		static void SetDropCallback(GLFWwindow* window, int count, const char** paths);

		static Window* s_ActiveWindow;

		GLFWwindow* m_GLFWwindow = nullptr;
		GLFWcursor* m_Cursor = nullptr;

		Vec2Int m_RestoreSize;
		Vec2Int m_RestorePos;

		static const GLFWvidmode* k_Videomode;

		static std::unique_ptr<Viewport> s_MainViewport;
		static bool s_IsVsync;
		static bool s_IsInitialized;

		EventCallbackFn m_EventCallback;
	};
}
