#include "pch.hpp"
#include "ImGuiRenderer.hpp"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>


namespace Bolt {

	void ImGuiRenderer::Initialize(GLFWwindow* window) {
		if (m_IsInitialized) {
			return;
		}

		BT_ASSERT(window != nullptr, "Window shouldn't be null!");

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigWindowsResizeFromEdges = false;

		BT_VERIFY(ImGui_ImplGlfw_InitForOpenGL(window, true), "Failed to init glfw for imgui!" );
		BT_VERIFY(ImGui_ImplOpenGL3_Init("#version 330 core"), "Failed to init openGL3 for imgui!");

		ApplyBoltTheme();

		m_IsInitialized = true;
	}

	void ImGuiRenderer::Shutdown() {
		if (!m_IsInitialized) {
			return;
		}

		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		m_IsInitialized = false;
	}

	void ImGuiRenderer::BeginFrame() {
		if (!m_IsInitialized) {
			return;
		}
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
	}

	void ImGuiRenderer::EndFrame() {
		if (!m_IsInitialized) {
			return;
		}
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}

	void ImGuiRenderer::ApplyBoltTheme() {
		ImGuiStyle& style = ImGui::GetStyle();

		// ── Sizing & Rounding ───────────────────────────────────────
		style.WindowPadding     = ImVec2(10, 10);
		style.FramePadding      = ImVec2(6, 4);
		style.CellPadding       = ImVec2(4, 3);
		style.ItemSpacing       = ImVec2(8, 5);
		style.ItemInnerSpacing  = ImVec2(5, 4);
		style.IndentSpacing     = 16.0f;
		style.ScrollbarSize     = 13.0f;
		style.GrabMinSize       = 8.0f;

		style.WindowRounding    = 4.0f;
		style.ChildRounding     = 3.0f;
		style.FrameRounding     = 3.0f;
		style.PopupRounding     = 3.0f;
		style.ScrollbarRounding = 6.0f;
		style.GrabRounding      = 2.0f;
		style.TabRounding       = 3.0f;

		style.WindowBorderSize  = 1.0f;
		style.FrameBorderSize   = 0.0f;
		style.PopupBorderSize   = 1.0f;
		style.TabBorderSize     = 0.0f;

		style.WindowMenuButtonPosition = ImGuiDir_None;
		style.SeparatorTextBorderSize  = 2.0f;

		// ── Colors ──────────────────────────────────────────────────
		ImVec4* c = style.Colors;

		// Base palette
		const ImVec4 bg        = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
		const ImVec4 bgChild   = ImVec4(0.13f, 0.13f, 0.15f, 1.00f);
		const ImVec4 bgPopup   = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);
		const ImVec4 surface   = ImVec4(0.18f, 0.18f, 0.21f, 1.00f);
		const ImVec4 surfaceHi = ImVec4(0.24f, 0.24f, 0.28f, 1.00f); // neutral hover
		const ImVec4 surfaceAct= ImVec4(0.28f, 0.28f, 0.33f, 1.00f); // neutral active
		const ImVec4 border    = ImVec4(0.24f, 0.24f, 0.28f, 0.65f);
		const ImVec4 accent    = ImVec4(0.33f, 0.53f, 0.84f, 1.00f); // only for focus/selection marks
		const ImVec4 text      = ImVec4(0.88f, 0.88f, 0.90f, 1.00f);
		const ImVec4 textDim   = ImVec4(0.50f, 0.50f, 0.54f, 1.00f);

		// Window
		c[ImGuiCol_WindowBg]             = bg;
		c[ImGuiCol_ChildBg]              = bgChild;
		c[ImGuiCol_PopupBg]              = bgPopup;

		// Borders
		c[ImGuiCol_Border]               = border;
		c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

		// Text
		c[ImGuiCol_Text]                 = text;
		c[ImGuiCol_TextDisabled]         = textDim;
		c[ImGuiCol_TextSelectedBg]       = ImVec4(accent.x, accent.y, accent.z, 0.35f);

		// Frame (input fields, checkboxes, sliders)
		c[ImGuiCol_FrameBg]              = surface;
		c[ImGuiCol_FrameBgHovered]       = surfaceHi;
		c[ImGuiCol_FrameBgActive]        = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);

		// Title bar
		c[ImGuiCol_TitleBg]              = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
		c[ImGuiCol_TitleBgActive]        = ImVec4(0.14f, 0.14f, 0.17f, 1.00f);
		c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.10f, 0.10f, 0.12f, 0.75f);

		// Menu bar
		c[ImGuiCol_MenuBarBg]            = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);

		// Scrollbar
		c[ImGuiCol_ScrollbarBg]          = ImVec4(0.10f, 0.10f, 0.12f, 0.53f);
		c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);
		c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.35f, 0.40f, 1.00f);
		c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.42f, 0.42f, 0.48f, 1.00f);

		// Buttons
		c[ImGuiCol_Button]               = surface;
		c[ImGuiCol_ButtonHovered]        = surfaceHi;
		c[ImGuiCol_ButtonActive]         = surfaceAct;

		// Headers (collapsing headers, tree nodes, selectable)
		c[ImGuiCol_Header]               = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
		c[ImGuiCol_HeaderHovered]        = surfaceHi;
		c[ImGuiCol_HeaderActive]         = surfaceAct;

		// Separator
		c[ImGuiCol_Separator]            = border;
		c[ImGuiCol_SeparatorHovered]     = ImVec4(0.36f, 0.36f, 0.42f, 1.00f);
		c[ImGuiCol_SeparatorActive]      = ImVec4(0.44f, 0.44f, 0.50f, 1.00f);

		// Resize grip
		c[ImGuiCol_ResizeGrip]           = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_ResizeGripHovered]    = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_ResizeGripActive]     = ImVec4(0, 0, 0, 0);

		// Tabs
		c[ImGuiCol_Tab]                  = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);
		c[ImGuiCol_TabHovered]           = surfaceHi;
		c[ImGuiCol_TabSelected]          = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
		c[ImGuiCol_TabSelectedOverline]  = accent;
		c[ImGuiCol_TabDimmed]            = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
		c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.17f, 0.17f, 0.20f, 1.00f);

		// Docking
		c[ImGuiCol_DockingPreview]       = ImVec4(accent.x, accent.y, accent.z, 0.40f);
		c[ImGuiCol_DockingEmptyBg]       = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);

		// Check mark / slider grab — accent is fine here (small focused elements)
		c[ImGuiCol_CheckMark]            = accent;
		c[ImGuiCol_SliderGrab]           = ImVec4(0.40f, 0.40f, 0.46f, 1.00f);
		c[ImGuiCol_SliderGrabActive]     = ImVec4(0.50f, 0.50f, 0.56f, 1.00f);

		// Drag drop
		c[ImGuiCol_DragDropTarget]       = ImVec4(accent.x, accent.y, accent.z, 0.70f);

		// Nav
		c[ImGuiCol_NavCursor]            = accent;

		// Table
		c[ImGuiCol_TableHeaderBg]        = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);
		c[ImGuiCol_TableBorderStrong]    = border;
		c[ImGuiCol_TableBorderLight]     = ImVec4(border.x, border.y, border.z, 0.40f);
		c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_TableRowBgAlt]        = ImVec4(1, 1, 1, 0.03f);

		// Modal dim
		c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0, 0, 0, 0.55f);
	}

}
