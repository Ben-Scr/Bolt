#include "pch.hpp"
#include "Profiling/Profiler.hpp"

#include "Core/Application.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>

// Belt-and-braces: premake also removefiles this TU when the profiler is stripped.
#ifdef AXIOM_PROFILER_ENABLED

namespace Axiom {

	namespace {
		// unique_ptr storage so the addresses returned by Register/Find stay
		// stable across subsequent push_back calls — vector<ProfilerModule>
		// would invalidate raw pointers on grow, which the AXIOM_PROFILE_*
		// caching path can hold on to.
		std::vector<std::unique_ptr<ProfilerModule>> g_Modules;
		std::unordered_map<std::string, size_t> g_Index;

		// Brief per-call lock; held to allow future worker-thread pushes without a rewrite.
		std::mutex g_Mutex;

		// Gates: (PanelVisible || BackgroundTracking) && IsPlaying && !IsPaused.
		bool g_PanelVisible = false;
		bool g_BackgroundTracking = false;

		int g_SamplingHz = 60;
		int g_TrackingSpan = 200;

		bool IsCollectingNow() {
			if (!g_PanelVisible && !g_BackgroundTracking) return false;

			// Tests run without an Application; skip the play/pause check there.
			if (Application::GetInstance() != nullptr) {
				if (!Application::GetIsPlaying()) return false;
				if (Application::IsPaused()) return false;
			}

			return true;
		}

		// Per-module timestamp; a shared timestamp would starve modules after the first.
		bool ShouldAcceptModuleSample(ProfilerModule& m) {
			if (!IsCollectingNow()) return false;
			if (g_SamplingHz <= 0) return true;

			using namespace std::chrono;
			const auto now = steady_clock::now();
			const auto interval = duration_cast<microseconds>(
				duration<double>(1.0 / static_cast<double>(g_SamplingHz)));
			if (now - m.LastPushTime < interval) return false;
			m.LastPushTime = now;
			return true;
		}

		// Maintains CurrentValue, RunningSum (incremental), Avg, Min/Max (rescan only on extremum eviction).
		void RecordSample(ProfilerModule& m, float value) {
			m.CurrentValue = value;

			if (m.Samples.size() != static_cast<size_t>(g_TrackingSpan)) {
				m.Samples.assign(g_TrackingSpan, 0.0f);
				m.Head = 0;
				m.Count = 0;
				m.RunningSum = 0.0;
			}

			const float overwritten = (m.Count == m.Samples.size())
				? m.Samples[m.Head]
				: 0.0f;

			m.Samples[m.Head] = value;
			m.Head = (m.Head + 1) % m.Samples.size();
			if (m.Count < m.Samples.size()) {
				m.Count++;
			}
			m.RunningSum += value - overwritten;
			m.AvgValue = m.Count > 0 ? static_cast<float>(m.RunningSum / m.Count) : 0.0f;

			// Min/Max: cheap path when the new sample doesn't dethrone the
			// current extremum and the overwritten sample wasn't itself
			// the extremum. Otherwise rescan. This keeps steady-state at
			// O(1) and worst-case (replacing the min or max) at O(N).
			const bool needRescan = (m.Count == m.Samples.size()) &&
				(overwritten == m.MinValue || overwritten == m.MaxValue);
			if (m.Count == 1) {
				m.MinValue = value;
				m.MaxValue = value;
			}
			else if (needRescan) {
				auto begin = m.Samples.begin();
				auto end = begin + static_cast<std::ptrdiff_t>(m.Count);
				const auto [mn, mx] = std::minmax_element(begin, end);
				m.MinValue = *mn;
				m.MaxValue = *mx;
			}
			else {
				m.MinValue = std::min(m.MinValue, value);
				m.MaxValue = std::max(m.MaxValue, value);
			}
		}

		void ClearRingBuffer(ProfilerModule& m) {
			std::fill(m.Samples.begin(), m.Samples.end(), 0.0f);
			m.Head = 0;
			m.Count = 0;
			m.RunningSum = 0.0;
			m.CurrentValue = 0.0f;
			m.AvgValue = 0.0f;
			m.MinValue = 0.0f;
			m.MaxValue = 0.0f;
			// Reset cadence so the next push isn't gated by a stale timestamp.
			m.LastPushTime = std::chrono::steady_clock::time_point{};
		}
	} // namespace

	void Profiler::Initialize() {
		std::scoped_lock lock(g_Mutex);
		g_Modules.clear();
		g_Index.clear();
		// Names are the contract between push sites and ProfilerPanel; order = panel display order.
		const char* names[] = {
			// Internal: hidden from panel, used to derive "Others" residual.
			"Frame Time",

			// CPU Usage category
			"Rendering",
			"Scripts",
			"Physics",
			"VSync",
			"Others",

			// Rendering category
			"Batches",
			"Triangles",
			"Vertices",

			// Memory category
			"Total Memory",
			"Texture Memory",
			"Entity Count",

			// Audio category
			"Playing Sources",

			// GPU category
			"GPU"
		};
		for (const char* name : names) {
			auto m = std::make_unique<ProfilerModule>();
			m->Name = name;
			m->Samples.assign(g_TrackingSpan, 0.0f);
			g_Index[name] = g_Modules.size();
			g_Modules.push_back(std::move(m));
		}
	}

	void Profiler::Shutdown() {
		std::scoped_lock lock(g_Mutex);
		g_Modules.clear();
		g_Index.clear();
		g_PanelVisible = false;
		g_BackgroundTracking = false;
	}

	ProfilerModule* Profiler::Register(const std::string& name) {
		std::scoped_lock lock(g_Mutex);
		auto it = g_Index.find(name);
		if (it != g_Index.end()) return g_Modules[it->second].get();

		auto m = std::make_unique<ProfilerModule>();
		m->Name = name;
		m->Samples.assign(g_TrackingSpan, 0.0f);
		ProfilerModule* raw = m.get();
		g_Index[name] = g_Modules.size();
		g_Modules.push_back(std::move(m));
		return raw;
	}

	ProfilerModule* Profiler::Find(const std::string& name) {
		std::scoped_lock lock(g_Mutex);
		auto it = g_Index.find(name);
		if (it == g_Index.end()) return nullptr;
		return g_Modules[it->second].get();
	}

	std::vector<ProfilerModule> Profiler::AllModules() {
		std::scoped_lock lock(g_Mutex);
		std::vector<ProfilerModule> snapshot;
		snapshot.reserve(g_Modules.size());
		for (const auto& mod : g_Modules) {
			snapshot.push_back(*mod);
		}
		return snapshot;
	}

	void Profiler::AllModules(std::vector<ProfilerModule>& outBuffer) {
		// Buffer-fill overload — lets the editor reuse storage across frames
		// instead of allocating a fresh vector + per-module sample buffers
		// on every panel render.
		std::scoped_lock lock(g_Mutex);
		outBuffer.clear();
		outBuffer.reserve(g_Modules.size());
		for (const auto& mod : g_Modules) {
			outBuffer.push_back(*mod);
		}
	}

	float Profiler::GetCurrentValue(const std::string& name) {
		std::scoped_lock lock(g_Mutex);
		auto it = g_Index.find(name);
		if (it == g_Index.end()) return 0.0f;
		return g_Modules[it->second]->CurrentValue;
	}

	void Profiler::PushValue(const std::string& name, float value) {
		std::scoped_lock lock(g_Mutex);
		auto it = g_Index.find(name);
		if (it == g_Index.end()) return;
		ProfilerModule& m = *g_Modules[it->second];
		if (!m.Enabled) return;
		if (!ShouldAcceptModuleSample(m)) return;
		RecordSample(m, value);
	}

	void Profiler::PushSample(const std::string& name, float milliseconds) {
		PushValue(name, milliseconds);
	}

	void Profiler::OnFrameMark(const std::string& /*name*/) {
		// Reserved hook — no per-frame work today, but a single chokepoint for future per-frame logic.
	}

	void Profiler::PushFrameDelta(float deltaSeconds) {
		const float frameMs = deltaSeconds * 1000.0f;

		std::scoped_lock lock(g_Mutex);
		if (auto it = g_Index.find("Frame Time"); it != g_Index.end()) {
			ProfilerModule& m = *g_Modules[it->second];
			if (m.Enabled && ShouldAcceptModuleSample(m)) RecordSample(m, frameMs);
		}
	}

	void Profiler::SetPanelVisible(bool visible) {
		std::scoped_lock lock(g_Mutex);
		g_PanelVisible = visible;
	}

	void Profiler::SetBackgroundTracking(bool enabled) {
		std::scoped_lock lock(g_Mutex);
		g_BackgroundTracking = enabled;
	}

	bool Profiler::IsCollecting() {
		std::scoped_lock lock(g_Mutex);
		if (!g_PanelVisible && !g_BackgroundTracking) return false;
		if (Application::GetInstance() != nullptr) {
			if (!Application::GetIsPlaying()) return false;
			if (Application::IsPaused()) return false;
		}
		return true;
	}

	void Profiler::SetSamplingHz(int hz) {
		std::scoped_lock lock(g_Mutex);
		g_SamplingHz = std::max(0, hz);
	}

	void Profiler::SetTrackingSpan(int span) {
		std::scoped_lock lock(g_Mutex);
		g_TrackingSpan = std::max(1, span);
		// Eager resize keeps the panel from showing a partial buffer at the wrong scale.
		for (auto& m : g_Modules) {
			ClearRingBuffer(*m);
			m->Samples.assign(g_TrackingSpan, 0.0f);
		}
	}

	int Profiler::GetSamplingHz() {
		std::scoped_lock lock(g_Mutex);
		return g_SamplingHz;
	}

	int Profiler::GetTrackingSpan() {
		std::scoped_lock lock(g_Mutex);
		return g_TrackingSpan;
	}

	void Profiler::SetModuleEnabled(const std::string& name, bool enabled) {
		std::scoped_lock lock(g_Mutex);
		auto it = g_Index.find(name);
		if (it == g_Index.end()) return;
		ProfilerModule& m = *g_Modules[it->second];
		if (m.Enabled == enabled) return;
		m.Enabled = enabled;
		if (!enabled) {
			ClearRingBuffer(m);
		}
	}

} // namespace Axiom

#endif // AXIOM_PROFILER_ENABLED
