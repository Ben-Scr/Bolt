// NOTE: this .cpp uses <imgui.h>. The Axiom-Engine premake EXCLUDES this
// file from the engine DLL's compile set. Axiom-Editor and Axiom-Runtime
// both pull it in via their own premake `files {}` entries — same pattern
// as `Axiom-Editor/src/Gui/ProfilerPanel.cpp` being compiled into Runtime.
//
// The header (StatsOverlay.hpp) only forward-declares ImVec2, so consumers
// that include the header but don't compile this .cpp don't pull ImGui.

#include "Diagnostics/StatsOverlay.hpp"

#include "Audio/AudioManager.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Core/Window.hpp"
#include "Graphics/Renderer2D.hpp"

#include <imgui.h>

#include <cstdio>
#include <string>

#ifdef AIM_PLATFORM_WINDOWS
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <psapi.h>
#  pragma comment(lib, "psapi.lib")
#endif

namespace Axiom::Diagnostics {

	namespace {
		std::string FormatBytesAsMb(std::size_t bytes) {
			const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%.1f MB", mb);
			return std::string(buf);
		}
	}

	bool StatsOverlay::RefreshIfDue(int screenWidth, int screenHeight) {
		const auto now = std::chrono::steady_clock::now();
		constexpr auto k_RefreshInterval = std::chrono::milliseconds(33); // ~30 Hz
		const bool firstCall = m_LastRefresh.time_since_epoch().count() == 0;
		if (!firstCall && (now - m_LastRefresh) < k_RefreshInterval) {
			// Always honour the latest screen size even on a no-refresh tick
			// so the displayed "Screen" line tracks resizes immediately
			// instead of lagging by up to 33 ms.
			m_Cached.ScreenWidth  = screenWidth;
			m_Cached.ScreenHeight = screenHeight;
			return false;
		}
		m_LastRefresh = now;

		auto* app = Application::GetInstance();
		// Stats reflect real wall-clock performance — use unscaled dt so TimeScale
		// doesn't distort the FPS / CPU readouts.
		const float dt = app ? app->GetTime().GetUnscaledDeltaTime() : 0.0f;
		m_Cached.Fps       = (dt > 0.0f) ? (1.0f / dt) : 0.0f;
		m_Cached.CpuMainMs = dt * 1000.0f;

		auto* renderer = app ? app->GetRenderer2D() : nullptr;
		if (renderer) {
			m_Cached.RenderThreadMs = renderer->GetRenderLoopDuration();
			const std::size_t inst = renderer->GetRenderedInstancesCount();
			m_Cached.Tris  = inst * 2u; // 2 triangles per instanced quad
			m_Cached.Verts = inst * 4u; // 4 vertices per instanced quad
		}
		else {
			m_Cached.RenderThreadMs = 0.0f;
			m_Cached.Tris  = 0;
			m_Cached.Verts = 0;
		}

		m_Cached.ScreenWidth  = screenWidth;
		m_Cached.ScreenHeight = screenHeight;

#ifdef AIM_PLATFORM_WINDOWS
		PROCESS_MEMORY_COUNTERS_EX pmc{};
		pmc.cb = sizeof(pmc);
		if (GetProcessMemoryInfo(
			GetCurrentProcess(),
			reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
			sizeof(pmc))) {
			// WorkingSetSize = bytes of physical RAM the process holds
			//                  ("allocated" / resident).
			// PrivateUsage   = committed virtual memory (Windows' closest
			//                  analogue of "reserved").
			m_Cached.MemAllocBytes    = static_cast<std::size_t>(pmc.WorkingSetSize);
			m_Cached.MemReservedBytes = static_cast<std::size_t>(pmc.PrivateUsage);
		}
#else
		m_Cached.MemAllocBytes    = 0;
		m_Cached.MemReservedBytes = 0;
#endif

		m_Cached.AudioPlaying = AudioManager::IsInitialized()
			? AudioManager::GetActiveSoundCount() : 0u;

		return true;
	}

	void StatsOverlay::RenderBody() const {
		const auto& s = m_Cached;
		ImGui::TextUnformatted("Statistics");
		ImGui::Separator();
		ImGui::Text("FPS:             %.1f", s.Fps);
		ImGui::Text("CPU main:        %.1f ms", s.CpuMainMs);
		ImGui::Text("Render thread:   %.1f ms", s.RenderThreadMs);
		ImGui::Text("Tris:            %zu", s.Tris);
		ImGui::Text("Verts:           %zu", s.Verts);
		ImGui::Separator();
		ImGui::Text("Screen:          %d x %d", s.ScreenWidth, s.ScreenHeight);
		ImGui::Separator();
		ImGui::Text("Allocated:       %s", FormatBytesAsMb(s.MemAllocBytes).c_str());
		ImGui::Text("Reserved:        %s", FormatBytesAsMb(s.MemReservedBytes).c_str());
		ImGui::Separator();
		ImGui::Text("Audio playing:   %u", s.AudioPlaying);
	}

	namespace {
		constexpr ImGuiWindowFlags k_OverlayFlags =
			ImGuiWindowFlags_NoDecoration |
			ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoFocusOnAppearing |
			ImGuiWindowFlags_NoNav |
			ImGuiWindowFlags_NoMove;
	}

	float StatsOverlay::RenderOverlayWindow(const char* uniqueId, const ImVec2& topRight, float yOffset) const {
		const ImVec2 overlayPos(topRight.x, topRight.y + yOffset);
		ImGui::SetNextWindowPos(overlayPos, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
		ImGui::SetNextWindowBgAlpha(0.78f);

		float consumedHeight = 0.0f;
		if (ImGui::Begin(uniqueId, nullptr, k_OverlayFlags)) {
			RenderBody();
			consumedHeight = ImGui::GetWindowSize().y;
		}
		ImGui::End();
		return consumedHeight;
	}

	float StatsOverlay::RenderInRect(const ImVec2& imageMin, const ImVec2& imageMax, float yOffset) const {
		constexpr float k_Pad = 8.0f;
		return RenderOverlayWindow("##AxiomStatsOverlayRect",
			ImVec2(imageMax.x - k_Pad, imageMin.y + k_Pad), yOffset);
	}

	float StatsOverlay::RenderInMainViewport(float yOffset) const {
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		if (!vp) return 0.0f;
		constexpr float k_Pad = 10.0f;
		return RenderOverlayWindow("##AxiomStatsOverlayViewport",
			ImVec2(vp->WorkPos.x + vp->WorkSize.x - k_Pad, vp->WorkPos.y + k_Pad), yOffset);
	}

} // namespace Axiom::Diagnostics
