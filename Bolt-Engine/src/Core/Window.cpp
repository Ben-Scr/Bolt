#include "pch.hpp"
#include "Window.hpp"
#include "Application.hpp"
#include "Graphics/Texture2D.hpp"
#include "Graphics/OpenGL.hpp"
#include "Events/WindowEvents.hpp"
#include "Events/KeyEvents.hpp"
#include "Events/MouseEvents.hpp"
#include <Utils/StringHelper.hpp>

#include <glad/glad.h>

#ifdef BT_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace Bolt {
	Window* Window::s_ActiveWindow = nullptr;
	bool Window::s_IsVsync = true;
	std::unique_ptr<Viewport> Window::s_MainViewport = nullptr;
	bool Window::s_IsInitialized = false;
	const GLFWvidmode* Window::k_Videomode = nullptr;

	Window::Window(int width, int height, const std::string& title)
	{
		Create(WindowSpecification{ width , height, title });
	}

	Window::Window(const WindowSpecification& props)
	{
		Create(props);
	}

	void Window::RefreshCallback(GLFWwindow* window) {
		(void)window;
		Application* app = Application::GetInstance();
		if (!app) return;
		app->RenderOnceForRefresh();
	}

	void Window::Initialize() {
		if (s_IsInitialized) {
			BT_CORE_WARN_TAG("Window", "GLFW is already initialized");
			return;
		}

		int code = glfwInit();

		BT_ASSERT(code == GLFW_TRUE, BoltErrorCode::Undefined, "GLFW library couldn't initialize, error code " + StringHelper::WrapWith(std::to_string(code), '\''));

		k_Videomode = glfwGetVideoMode(GetMainMonitor());
		s_IsInitialized = true;
	}

	void Window::Shutdown() {
		if (!s_IsInitialized) {
			return;
		}

		s_MainViewport.reset();
		s_ActiveWindow = nullptr;
		k_Videomode = nullptr;
		s_IsInitialized = false;
		glfwTerminate();
	}

	void Window::FocusCallback(GLFWwindow* window, int focused) {
		Window* win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
		Application::SetWindowFocused(static_cast<bool>(focused));

		if ((bool)focused) {
			if (win && win->m_EventCallback) {
				WindowFocusEvent e;
				win->m_EventCallback(e);
			}
		}
		else {
			if (win && win->m_EventCallback) {
				WindowLostFocusEvent e;
				win->m_EventCallback(e);
			}
		}
	}

	void Window::Create(const WindowSpecification& props) {
		BT_ASSERT(s_IsInitialized, BoltErrorCode::NotInitialized, "The Window isn't initialized");

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_SAMPLES, 8);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);


		s_MainViewport = std::make_unique<Viewport>(
			props.Fullscreen ? k_Videomode->width : props.Width,
			props.Fullscreen ? k_Videomode->height : props.Height
		);

		m_GLFWwindow = glfwCreateWindow(s_MainViewport->GetWidth(), s_MainViewport->GetHeight(), props.Title.c_str(), nullptr, nullptr);
		BT_ASSERT(m_GLFWwindow, BoltErrorCode::Undefined, "Failed to create window!");

		SetDecorated(props.Decorated);
		SetResizeable(props.Resizeable);

		if (props.Fullscreen)
			SetFullScreen(true);
		else
			CenterWindow();

		glfwMakeContextCurrent(m_GLFWwindow);
		glfwSetWindowUserPointer(m_GLFWwindow, this);

		glfwSetKeyCallback(m_GLFWwindow, SetKeyCallback);
		glfwSetMouseButtonCallback(m_GLFWwindow, SetMouseButtonCallback);
		glfwSetCursorPosCallback(m_GLFWwindow, SetCursorPositionCallback);
		glfwSetScrollCallback(m_GLFWwindow, SetScrollCallback);
		glfwSetWindowFocusCallback(m_GLFWwindow, FocusCallback);

		glfwSetFramebufferSizeCallback(m_GLFWwindow, SetWindowResizedCallback);

		glfwSetDropCallback(m_GLFWwindow, SetDropCallback);
		glfwSetWindowRefreshCallback(m_GLFWwindow, RefreshCallback);

		glfwSwapInterval(s_IsVsync ? 1 : 0);
		SyncViewportFromFramebuffer();

		s_ActiveWindow = this;
	}
	void Window::Destroy() {
		if (!m_GLFWwindow) {
			return;
		}

		if (m_Cursor) {
			glfwDestroyCursor(m_Cursor);
			m_Cursor = nullptr;
		}
		glfwSetWindowUserPointer(m_GLFWwindow, nullptr);
		m_EventCallback = {};
		glfwDestroyWindow(m_GLFWwindow);
		m_GLFWwindow = nullptr;
		if (s_ActiveWindow == this) {
			s_ActiveWindow = nullptr;
		}
	}

	void Window::CenterWindow() {
		Vec2Int screenCenter = GetScreenCenter();
		int windowWidth, windowHeight;
		glfwGetWindowSize(m_GLFWwindow, &windowWidth, &windowHeight);
		int posX = screenCenter.x - windowWidth / 2;
		int posY = screenCenter.y - windowHeight / 2;
		glfwSetWindowPos(m_GLFWwindow, posX, posY);
	}
	void Window::Focus() {
		glfwFocusWindow(m_GLFWwindow);
	}
	void Window::MaximizeWindow(bool reset) {
		if (reset && IsMaximized()) {
			glfwRestoreWindow(m_GLFWwindow);
		}
		else {
			glfwMaximizeWindow(m_GLFWwindow);
		}

		SyncViewportFromFramebuffer();
		UpdateViewport();
	}
	void Window::MinimizeWindow() {
		glfwIconifyWindow(m_GLFWwindow);
	}
	void Window::RestoreWindow() {
		glfwRestoreWindow(m_GLFWwindow);
		SyncViewportFromFramebuffer();
		UpdateViewport();
	}

	void Window::SetKeyCallback(GLFWwindow* window, int key, int, int action, int mods) {
		(void)mods;
		if (key == GLFW_KEY_UNKNOWN) {
			return;
		}

		Application* app = Application::GetInstance();
		if (!app) return;

		Window* win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));

		switch (action) {
		case GLFW_PRESS:
			app->m_Input.OnKeyDown(key);
			if (win && win->m_EventCallback) {
				KeyPressedEvent e(key, false);
				win->m_EventCallback(e);
			}
			break;
		case GLFW_RELEASE:
			app->m_Input.OnKeyUp(key);
			if (win && win->m_EventCallback) {
				KeyReleasedEvent e(key);
				win->m_EventCallback(e);
			}
			break;
		case GLFW_REPEAT:
			app->m_Input.OnKeyDown(key);
			if (win && win->m_EventCallback) {
				KeyPressedEvent e(key, true);
				win->m_EventCallback(e);
			}
			break;
		default:
			break;
		}
	}

	void Window::SetMouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
		(void)mods;
		Application* app = Application::GetInstance();
		if (!app) return;

		Window* win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));

		switch (action) {
		case GLFW_PRESS:
			app->m_Input.OnMouseDown(button);
			if (win && win->m_EventCallback) {
				MouseButtonPressedEvent e(button);
				win->m_EventCallback(e);
			}
			break;
		case GLFW_RELEASE:
			app->m_Input.OnMouseUp(button);
			if (win && win->m_EventCallback) {
				MouseButtonReleasedEvent e(button);
				win->m_EventCallback(e);
			}
			break;
		default:
			break;
		}
	}

	void Window::SetCursorPositionCallback(GLFWwindow* window, double xPos, double yPos) {
		Application* app = Application::GetInstance();
		if (!app) return;
		app->m_Input.OnMouseMove(xPos, yPos);

		Window* win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
		if (win && win->m_EventCallback) {
			MouseMovedEvent e(static_cast<float>(xPos), static_cast<float>(yPos));
			win->m_EventCallback(e);
		}
	}

	void Window::SetScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
		Application* app = Application::GetInstance();
		if (!app) return;
		app->m_Input.OnScroll(static_cast<float>(yoffset));

		Window* win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
		if (win && win->m_EventCallback) {
			MouseScrolledEvent e(static_cast<float>(xoffset), static_cast<float>(yoffset));
			win->m_EventCallback(e);
		}
	}

	void Window::SetFullScreen(bool enabled) {
		bool isFullScreen = IsFullScreen();

		if ((isFullScreen && enabled) || (!isFullScreen && !enabled))
			return;

		if (enabled) {
			m_RestoreSize = GetSize();
			m_RestorePos = GetPosition();
		}

		glfwSetWindowMonitor(
			m_GLFWwindow,
			enabled ? GetMainMonitor() : nullptr,
			enabled ? 0 : m_RestorePos.x,
			enabled ? 0 : m_RestorePos.y,
			enabled ? k_Videomode->width : m_RestoreSize.x,
			enabled ? k_Videomode->height : m_RestoreSize.y,
			k_Videomode->refreshRate
		);
		SyncViewportFromFramebuffer();
		UpdateViewport();

		if (!enabled) {
			SetSize(m_RestoreSize);
			SetPosition(m_RestorePos);
		}
	}

	void Window::SetDecorated(bool enabled) {
		glfwSetWindowAttrib(m_GLFWwindow, GLFW_DECORATED, enabled ? GLFW_TRUE : GLFW_FALSE);
	}
	void Window::SetVisible(bool enabled) {
		if (enabled)
			glfwShowWindow(m_GLFWwindow);
		else
			glfwHideWindow(m_GLFWwindow);
	}

	void Window::SetResizeable(bool enabled) {
		glfwSetWindowAttrib(m_GLFWwindow, GLFW_RESIZABLE, enabled ? GLFW_TRUE : GLFW_FALSE);
	}

	void Window::SetCursorLocked(bool enabled) {
		glfwSetInputMode(m_GLFWwindow, GLFW_CURSOR, enabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
	}
	void Window::SetCursorHidden(bool enabled) {
		glfwSetInputMode(m_GLFWwindow, GLFW_CURSOR, enabled ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_NORMAL);
	}
	void Window::SetCursorImage(const Texture2D* tex2D) {
		if (!tex2D) {
			BT_WARN_TAG("Window", "Cannot set cursor image from null texture");
			return;
		}

		std::unique_ptr<ImageData> imgData = tex2D->GetImageData();
		if (!imgData || !imgData->Pixels) {
			BT_WARN_TAG("Window", "Cannot set cursor image from invalid texture data");
			return;
		}

		GLFWimage img;
		img.width = imgData->Width;
		img.height = imgData->Height;
		img.pixels = imgData->Pixels;

		int xhot = 0;
		int yhot = 0;

		GLFWcursor* newCursor = glfwCreateCursor(&img, xhot, yhot);
		if (!newCursor)
			return;

		glfwSetCursor(m_GLFWwindow, newCursor);

		if (m_Cursor)
			glfwDestroyCursor(m_Cursor);

		m_Cursor = newCursor;
	}

	void Window::SetWindowIcon(const Texture2D* tex2D) {
		BT_ASSERT(tex2D, BoltErrorCode::NullReference, "Texture is null");
		std::unique_ptr<ImageData> imgData(tex2D->GetImageData());
		BT_ASSERT(imgData, BoltErrorCode::NullReference, "Image data is null");


		const int w = imgData->Width;
		const int h = imgData->Height;

		BT_ASSERT(w > 0 && h > 0, BoltErrorCode::InvalidValue, "Icon size must be > 0");
		BT_ASSERT(imgData->Pixels != nullptr, BoltErrorCode::NullReference, "Icon pixels null");

		imgData->FlipVerticalRGBA();

		GLFWimage img;
		img.width = imgData->Width;
		img.height = imgData->Height;
		img.pixels = imgData->Pixels;

		glfwSetWindowIcon(m_GLFWwindow, 1, &img);
	}

	void Window::SetWindowIconFromResource() {
#ifdef BT_PLATFORM_WINDOWS
		HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(1));
		if (!hIcon) {
			BT_WARN_TAG("Window", "No embedded icon resource found.");
			return;
		}

		ICONINFO iconInfo{};
		if (!GetIconInfo(hIcon, &iconInfo)) {
			DestroyIcon(hIcon);
			BT_WARN_TAG("Window", "Failed to get icon info.");
			return;
		}

		BITMAP bm{};
		GetObject(iconInfo.hbmColor, sizeof(bm), &bm);

		int w = bm.bmWidth;
		int h = bm.bmHeight;

		std::vector<unsigned char> pixels(w * h * 4);

		BITMAPINFOHEADER bi{};
		bi.biSize = sizeof(bi);
		bi.biWidth = w;
		bi.biHeight = -h; // top-down
		bi.biPlanes = 1;
		bi.biBitCount = 32;
		bi.biCompression = BI_RGB;

		HDC hdc = GetDC(NULL);
		GetDIBits(hdc, iconInfo.hbmColor, 0, h, pixels.data(),
			reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
		ReleaseDC(NULL, hdc);

		// Windows gives BGRA, GLFW expects RGBA
		for (int i = 0; i < w * h * 4; i += 4)
			std::swap(pixels[i], pixels[i + 2]);

		DeleteObject(iconInfo.hbmColor);
		DeleteObject(iconInfo.hbmMask);
		DestroyIcon(hIcon);

		GLFWimage img;
		img.width = w;
		img.height = h;
		img.pixels = pixels.data();

		glfwSetWindowIcon(m_GLFWwindow, 1, &img);
#else
		BT_WARN_TAG("Window", "SetWindowIconFromResource is only supported on Windows.");
#endif
	}

	void Window::SetWindowResizedCallback(GLFWwindow* window, int width, int height) {
		Window* win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
		if (!win) return;

		win->SyncViewportFromFramebuffer();
		win->UpdateViewport();

		if (win->m_EventCallback) {
			WindowResizeEvent e(width, height);
			win->m_EventCallback(e);
		}
	}

	Vec2 Window::GetCursorPosition() const {
		double x, y;
		glfwGetCursorPos(m_GLFWwindow, &x, &y);
		return Vec2(x, y);
	}
	std::string Window::GetClipboardString() const {
		const char* clipboard = glfwGetClipboardString(m_GLFWwindow);
		return clipboard ? std::string(clipboard) : std::string{};
	}
	Vec2Int Window::GetScreenCenter() const {
		return Vec2Int(k_Videomode->width / 2, k_Videomode->height / 2);
	}
	Vec2 Window::GetWindowCenter() const {
		Vec2Int size = GetSize();
		return Vec2(size.x / 2.f, size.y / 2.f);
	}
	GLFWmonitor* Window::GetWindowMonitor() const { return glfwGetWindowMonitor(m_GLFWwindow); }
	GLFWmonitor* Window::GetMainMonitor() { return glfwGetPrimaryMonitor(); }

	bool Window::IsMaximized() const {
		return glfwGetWindowAttrib(m_GLFWwindow, GLFW_MAXIMIZED);
	}
	bool Window::IsMinimized() const {
		return glfwGetWindowAttrib(m_GLFWwindow, GLFW_ICONIFIED);
	}
	bool Window::IsFullScreen()const {
		return GetWindowMonitor() != nullptr;
	}
	bool Window::IsVisible() const {
		return glfwGetWindowAttrib(m_GLFWwindow, GLFW_VISIBLE) == GLFW_TRUE;
	}
	bool Window::IsDecorated()const {
		return glfwGetWindowAttrib(m_GLFWwindow, GLFW_DECORATED);
	}
	bool Window::IsResizeable() const {
		return glfwGetWindowAttrib(m_GLFWwindow, GLFW_RESIZABLE);
	}


	void Window::SetDropCallback(GLFWwindow* window, int count, const char** paths) {
		Window* win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
		if (!win || !win->m_EventCallback || count <= 0) return;

		std::vector<std::string> filePaths;
		filePaths.reserve(count);
		for (int i = 0; i < count; i++) {
			filePaths.emplace_back(paths[i]);
		}

		FileDropEvent e(std::move(filePaths));
		win->m_EventCallback(e);
	}

	void Window::UpdateViewport() {
		if (OpenGL::IsInitialized())
		{
			glViewport(0, 0, s_MainViewport->GetWidth(), s_MainViewport->GetHeight());
		}
	}

	void Window::SyncViewportFromFramebuffer() {
		if (!m_GLFWwindow) {
			return;
		}

		int width = 0;
		int height = 0;
		glfwGetFramebufferSize(m_GLFWwindow, &width, &height);

		if (!s_MainViewport) {
			s_MainViewport = std::make_unique<Viewport>(width, height);
			return;
		}

		s_MainViewport->SetWidth(width);
		s_MainViewport->SetHeight(height);
	}
}
