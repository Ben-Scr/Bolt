#pragma once

// ─── Axiom Profiler ─────────────────────────────────────────────────────────
//
// A two-layer profiler:
//
//   1. Tracy backend  — every AXIOM_PROFILE_* macro emits a Tracy zone /
//      frame mark / plot, so the standalone Tracy viewer can be attached
//      over TCP for deep sessions, hitch hunting, and call-stack capture.
//
//   2. In-engine ring-buffer collector — the SAME macros, when matched
//      against a registered named module, push their value into a fixed-
//      size ring buffer the in-engine ImGui ProfilerPanel reads from.
//      This is what populates the at-a-glance dashboard inside the editor.
//
// The two layers run in parallel from one instrumentation site. Tracy is
// the source of truth for *tracing* (what zones, in what order, on which
// thread). The Profiler ring buffers are the source of truth for the
// in-engine *summary* dashboard (rolling avg, min, max, history graph).
//
// Compile-time gating: when AXIOM_PROFILER_ENABLED is undefined, every
// macro expands to (void)0, every API call is a no-op stub, and the .cpp
// files are excluded from the build by premake. There is zero binary or
// runtime cost in stripped builds.
//
// Pass --no-profiler at premake time to strip.
// ────────────────────────────────────────────────────────────────────────────

#include "Core/Export.hpp"

#ifdef AXIOM_PROFILER_ENABLED
// MSVC quirk: both `__FUNCTION__` and `__func__` are `static const char[]`,
// not string literals. Their address is not a compile-time constant, so
// Tracy's `static constexpr SourceLocationData { ..., function, ... }`
// fails with C2131. We override TracyFunction to the empty string literal
// "":
//   - It's a real string literal, so it stays constexpr-valid for the
//     ZoneScopedN code path (had we used it).
//   - We use ZoneTransientN (transient zones) which internally calls
//     strlen(TracyFunction) — and `strlen(nullptr)` is UB that Release-
//     mode MSVC crashes on, while `strlen("")` is a clean 0. Using ""
//     avoids both pitfalls.
//   - The displayed Tracy zone keeps the *name* we pass ("Frame", etc.);
//     the function field just shows up empty in the viewer, which is fine.
// Must be defined BEFORE <tracy/Tracy.hpp>.
#  ifndef TracyFunction
#    define TracyFunction ""
#  endif
#  include <tracy/Tracy.hpp>
#endif

#include <cstddef>
#include <string>
#include <vector>
#include <chrono>

namespace Axiom {

	// ── Module bookkeeping ────────────────────────────────────────────────
	//
	// One module per topic the panel cares about (FPS, Frame, Physics, GPU,
	// DrawCalls, GpuMemory, RAM, Entities). The macro instrumentation looks
	// up modules by name; if no module matches, Tracy still gets the call
	// but the in-process collector skips. This keeps Tracy's overhead pinned
	// to its own (very low) cost regardless of which modules are enabled.
	struct AXIOM_API ProfilerModule {
		std::string Name;
		bool Enabled = true;     // panel checkbox
		bool Available = true;   // GPU memory may flip this off when no driver ext

		// Ring buffer: head index + valid count. Capacity is the panel's
		// "Tracking Span" setting; resized via Profiler::SetTrackingSpan().
		std::vector<float> Samples;
		size_t Head = 0;
		size_t Count = 0;

		float CurrentValue = 0.0f;
		float AvgValue = 0.0f;
		float MinValue = 0.0f;
		float MaxValue = 0.0f;

		// Cached running sum so Avg is O(1). Min/Max are recomputed only
		// when Samples mutate (in PushSample) so the panel render is also O(1).
		double RunningSum = 0.0;

		// Per-module sampling-rate gate. Was previously a single global
		// timestamp shared across all modules — that meant the first push
		// per ~16ms (typically PushFrameDelta) consumed the gate and every
		// other module's push got dropped, leaving them perpetually at 0.0.
		// Per-module timestamps mean each module observes its own 1/Hz
		// cadence independently.
		std::chrono::steady_clock::time_point LastPushTime{};
	};

#ifdef AXIOM_PROFILER_ENABLED
	class AXIOM_API Profiler {
	public:
		// Called once at engine init. Registers the eight built-in modules
		// (idempotent — repeated calls clear and re-register).
		static void Initialize();

		// Called at engine shutdown. Drops all modules + clears state.
		static void Shutdown();

		// Module registration. Idempotent: registering an existing name
		// just returns the existing module (lets call sites be defensive).
		static ProfilerModule* Register(const std::string& name);

		// Lookups. nullptr if no such module.
		static ProfilerModule* Find(const std::string& name);
		static std::vector<ProfilerModule> AllModules();

		// Read the most recent CurrentValue of a module by name. 0.0 if no
		// such module or no samples have been recorded yet. Used by the
		// "Others" residual computation in Application::Run to derive the
		// unattributed CPU time per frame.
		static float GetCurrentValue(const std::string& name);

		// Push an already-computed numeric sample into a named module.
		// Used for non-time metrics (DrawCalls, Entities, RAM, GPU memory).
		// Honors the visibility / enabled / sampling-rate gates.
		static void PushValue(const std::string& name, float value);

		// Push a duration-in-milliseconds sample. Same gates as PushValue.
		// Used by AXIOM_PROFILE_SCOPE's RAII helper and by the GPU timer.
		static void PushSample(const std::string& name, float milliseconds);

		// Frame-loop hooks.
		// OnFrameMark advances the sampling-rate cadence (so a 60→10 Hz
		// change actually drops samples instead of pushing every call). It
		// is called from AXIOM_PROFILE_FRAME at the bottom of the frame.
		// PushFrameDelta records the FPS + Frame Time modules from one
		// shared dt measurement so they can't drift apart.
		static void OnFrameMark(const std::string& name);
		static void PushFrameDelta(float deltaSeconds);

		// Gates.
		// Both are required for samples to land. SetPanelVisible is driven
		// by the panel's Render() — when the panel isn't drawn, it sets
		// visible=false. The background-tracking toggle keeps collection
		// alive when the panel is closed; defaults to false.
		static void SetPanelVisible(bool visible);
		static void SetBackgroundTracking(bool enabled);
		static bool IsCollecting();

		// Sampling configuration. Both persisted by the editor across sessions.
		static void SetSamplingHz(int hz);
		static void SetTrackingSpan(int span);
		static int  GetSamplingHz();
		static int  GetTrackingSpan();

		// Per-module enable flag. Disabling clears the module's ring buffer.
		static void SetModuleEnabled(const std::string& name, bool enabled);

	private:
		Profiler() = delete;
	};
#else
	// Stripped-build stubs. Inline so call sites compile without #ifdef
	// guards; the compiler emits no code for any of these. Returning safe
	// defaults so any callers that branch on a value (e.g. IsCollecting)
	// still produce sane behavior.
	class Profiler {
	public:
		static inline void Initialize() {}
		static inline void Shutdown() {}
		static inline ProfilerModule* Register(const std::string&) { return nullptr; }
		static inline ProfilerModule* Find(const std::string&) { return nullptr; }
		static inline std::vector<ProfilerModule> AllModules() { return {}; }
		static inline float GetCurrentValue(const std::string&) { return 0.0f; }
		static inline void PushValue(const std::string&, float) {}
		static inline void PushSample(const std::string&, float) {}
		static inline void OnFrameMark(const std::string&) {}
		static inline void PushFrameDelta(float) {}
		static inline void SetPanelVisible(bool) {}
		static inline void SetBackgroundTracking(bool) {}
		static inline bool IsCollecting() { return false; }
		static inline void SetSamplingHz(int) {}
		static inline void SetTrackingSpan(int) {}
		static inline int  GetSamplingHz() { return 60; }
		static inline int  GetTrackingSpan() { return 200; }
		static inline void SetModuleEnabled(const std::string&, bool) {}
	private:
		Profiler() = delete;
	};
#endif

	// ── Macro plumbing ────────────────────────────────────────────────────

#ifdef AXIOM_PROFILER_ENABLED

	// RAII helper for AXIOM_PROFILE_SCOPE. Captures a steady-clock start
	// timestamp at construction; on destruction computes the elapsed
	// milliseconds and pushes them through Profiler::PushSample. The
	// in-flight cost is one chrono::now() pair — comparable to Tracy's
	// own zone overhead. When the named module isn't registered the push
	// is dropped at the lookup, which is cheap.
	class AXIOM_API ProfilerScopedSample {
	public:
		explicit ProfilerScopedSample(const char* name)
			: m_Name(name)
			, m_Start(std::chrono::steady_clock::now()) {
		}
		~ProfilerScopedSample() {
			using namespace std::chrono;
			const auto end = steady_clock::now();
			const float ms = duration<float, std::milli>(end - m_Start).count();
			Profiler::PushSample(m_Name, ms);
		}
		ProfilerScopedSample(const ProfilerScopedSample&) = delete;
		ProfilerScopedSample& operator=(const ProfilerScopedSample&) = delete;
	private:
		const char* m_Name;
		std::chrono::steady_clock::time_point m_Start;
	};

	// Token-pasting helper so two macros expanded on the same source line
	// don't clash on the local variable name.
	#define AIM_PROF_CAT_INNER(a, b) a##b
	#define AIM_PROF_CAT(a, b) AIM_PROF_CAT_INNER(a, b)

	// Mark a scope. Uses Tracy's transient zone API (string passed at
	// runtime) rather than ZoneScopedN, because the constexpr-requiring
	// SourceLocationData chokes on MSVC's non-literal __FUNCTION__/__func__
	// even with TracyFunction overrides. Transient zones cost a strlen()
	// per push — negligible vs. the rest of frame work, and Tracy's docs
	// explicitly endorse this for cases like this. The named zone still
	// shows up correctly in Tracy's timeline.
	#define AXIOM_PROFILE_SCOPE(name)                                       \
		ZoneTransientN(AIM_PROF_CAT(_aim_zone_, __LINE__), name, true);     \
		::Axiom::ProfilerScopedSample AIM_PROF_CAT(_aim_prof_, __LINE__)(name)

	// Mark end-of-frame. Tracy frame mark + sampling cadence advance.
	#define AXIOM_PROFILE_FRAME(name)                                       \
		FrameMarkNamed(name);                                               \
		::Axiom::Profiler::OnFrameMark(name)

	// Push a non-time numeric value (draw calls, entity count, RAM MB).
	// Tracy plot + ring-buffer push.
	#define AXIOM_PROFILE_VALUE(name, val)                                  \
		do {                                                                \
			TracyPlot(name, double(val));                                   \
			::Axiom::Profiler::PushValue(name, float(val));                 \
		} while (0)

#else // !AXIOM_PROFILER_ENABLED — strip everything

	#define AXIOM_PROFILE_SCOPE(name)      ((void)0)
	#define AXIOM_PROFILE_FRAME(name)      ((void)0)
	#define AXIOM_PROFILE_VALUE(name, val) ((void)0)

#endif

} // namespace Axiom
