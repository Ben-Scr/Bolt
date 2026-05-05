#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

// Forward decl so this header doesn't drag ImGui into the engine's public
// surface. The .cpp uses <imgui.h> directly. The .cpp is intentionally
// EXCLUDED from the engine DLL's compile set (see Axiom-Engine/premake5.lua)
// and pulled in by both Axiom-Editor and Axiom-Runtime where ImGui is linked.
//
// NOTE: deliberately NOT marked AXIOM_API. Each consumer (Editor, Runtime)
// compiles the .cpp into its own binary; there is no DLL boundary for this
// class. Adding AXIOM_API would make MSVC look for the symbols in
// Axiom-Engine.dll where they don't exist (the .cpp is excluded), causing
// LNK2019.
struct ImVec2;

namespace Axiom::Diagnostics {

	// Engine-level runtime stats overlay. Same data + UI shape as Unity's
	// "Statistics" overlay: FPS, CPU/render time, tris/verts, screen size,
	// memory, audio.
	//
	// Designed for two consumers:
	//   • Editor — drawn pinned to the top-right of the Game View FBO image
	//     via RenderInRect(imageMin, imageMax).
	//   • Runtime — drawn pinned to the top-right of the application window
	//     via Render(). The runtime ImGui host (RuntimeStatsLayer) owns the
	//     context; this class just calls ImGui::Begin/End between the host's
	//     NewFrame/Render bracket.
	//
	// Cache refresh rate is 30 Hz so animated counters don't flicker every
	// frame. RefreshIfDue() is cheap and idempotent — call it from the
	// per-frame path before any Render*() call. The first call always
	// refreshes regardless of the timer.
	class StatsOverlay {
	public:
		struct Snapshot {
			float Fps              = 0.0f;
			float CpuMainMs        = 0.0f;
			float RenderThreadMs   = 0.0f;
			std::size_t Tris       = 0;
			std::size_t Verts      = 0;
			int   ScreenWidth      = 0;
			int   ScreenHeight     = 0;
			std::size_t MemAllocBytes    = 0; // process working set (resident)
			std::size_t MemReservedBytes = 0; // committed virtual memory
			std::uint32_t AudioPlaying   = 0;
		};

		// Refresh the cached snapshot at most every 33 ms (~30 Hz). Pass
		// the screen dimensions you'd like reported (e.g. the editor passes
		// the FBO size; the runtime passes the window size). Returns true
		// when a refresh actually fired.
		bool RefreshIfDue(int screenWidth, int screenHeight);

		const Snapshot& GetCached() const { return m_Cached; }

		// Render the stats window pinned to the top-right of the given
		// rectangle, optionally offset down by `yOffset`. Use this for the
		// editor's Game View where the rect is the FBO image area.
		// Returns the height of the rendered window so the caller can stack
		// another overlay (e.g. logs) below it.
		float RenderInRect(const ImVec2& imageMin, const ImVec2& imageMax, float yOffset = 0.0f) const;

		// Render the stats window pinned to the top-right of the active
		// ImGui main viewport, optionally offset down by `yOffset`. Use this
		// from a runtime layer that owns the fullscreen ImGui frame.
		// Returns the rendered window height so callers can stack.
		float RenderInMainViewport(float yOffset = 0.0f) const;

	private:
		void RenderBody() const; // shared body of RenderInRect / RenderInMainViewport
		float RenderOverlayWindow(const char* uniqueId, const ImVec2& topRight, float yOffset) const;

		Snapshot m_Cached{};
		std::chrono::steady_clock::time_point m_LastRefresh{};
	};

} // namespace Axiom::Diagnostics
