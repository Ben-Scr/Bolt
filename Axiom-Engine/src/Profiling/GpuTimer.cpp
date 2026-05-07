#include "pch.hpp"
#include "Profiling/GpuTimer.hpp"

#ifdef AXIOM_PROFILER_ENABLED

#include "Profiling/Profiler.hpp"

#include <glad/glad.h>

#include <array>
#include <cstring>

namespace Axiom {

	namespace {
		constexpr size_t kFrameRingSize = 3;
	}

	struct GpuTimer::PerFrame {
		GLuint Query = 0;
		bool Pending = false;
	};

	GpuTimer::GpuTimer() = default;

	GpuTimer::~GpuTimer() {
		Shutdown();
	}

	void GpuTimer::Initialize() {
		if (m_Initialized) return;
		m_Frames = new PerFrame[kFrameRingSize];
		for (size_t i = 0; i < kFrameRingSize; i++) {
			glGenQueries(1, &m_Frames[i].Query);
			m_Frames[i].Pending = false;
		}
		m_NextWrite = 0;
		m_NextRead = 0;
		m_PendingCount = 0;
		m_FrameOpen = false;
		m_Initialized = true;
	}

	void GpuTimer::Shutdown() {
		if (!m_Initialized) return;
		// Best-effort: dead GL context is a no-op on most drivers.
		for (size_t i = 0; i < kFrameRingSize; i++) {
			if (m_Frames[i].Query != 0) {
				glDeleteQueries(1, &m_Frames[i].Query);
				m_Frames[i].Query = 0;
			}
		}
		delete[] m_Frames;
		m_Frames = nullptr;
		m_Initialized = false;
		m_FrameOpen = false;
	}

	void GpuTimer::BeginFrame() {
		if (!m_Initialized) return;
		if (m_FrameOpen) return; // mismatched call — drop silently

		// Ring full: drop oldest pending query to keep cadence stable. The previous
		// order incremented m_NextRead BEFORE clearing Pending, marking the
		// next-oldest slot as cleared while leaving the actual oldest stuck Pending
		// forever. Subsequent glBeginQuery on a still-Pending query returns
		// GL_INVALID_OPERATION and the GPU sample silently stops updating.
		if (m_PendingCount >= kFrameRingSize) {
			m_Frames[m_NextRead].Pending = false;          // clear the OLDEST first
			m_NextRead = (m_NextRead + 1) % kFrameRingSize; // then advance
			m_PendingCount--;
		}

		PerFrame& slot = m_Frames[m_NextWrite];
		glBeginQuery(GL_TIME_ELAPSED, slot.Query);
		m_FrameOpen = true;
	}

	void GpuTimer::EndFrame() {
		if (!m_Initialized) return;
		if (!m_FrameOpen) return;

		glEndQuery(GL_TIME_ELAPSED);
		m_Frames[m_NextWrite].Pending = true;
		m_NextWrite = (m_NextWrite + 1) % kFrameRingSize;
		m_PendingCount++;
		m_FrameOpen = false;
	}

	void GpuTimer::PollAndPublish() {
		if (!m_Initialized) return;
		if (m_PendingCount == 0) return;

		PerFrame& slot = m_Frames[m_NextRead];
		if (!slot.Pending) return;

		GLint available = GL_FALSE;
		glGetQueryObjectiv(slot.Query, GL_QUERY_RESULT_AVAILABLE, &available);
		if (available == GL_FALSE) {
			// Result not ready; wait one more frame.
			return;
		}

		GLuint64 elapsedNs = 0;
		glGetQueryObjectui64v(slot.Query, GL_QUERY_RESULT, &elapsedNs);

		const float ms = static_cast<float>(elapsedNs) / 1'000'000.0f;
		Profiler::PushSample("GPU", ms);

		slot.Pending = false;
		m_NextRead = (m_NextRead + 1) % kFrameRingSize;
		m_PendingCount--;
	}

	long long GpuTimer::QueryGpuMemoryMb() {
		// NVIDIA NVX_gpu_memory_info: in-use = total - available (KB).
		constexpr GLenum NVX_TOTAL_KB     = 0x9048;
		constexpr GLenum NVX_AVAILABLE_KB = 0x9049;

		while (glGetError() != GL_NO_ERROR) {}

		GLint totalKb = 0;
		glGetIntegerv(NVX_TOTAL_KB, &totalKb);
		if (glGetError() == GL_NO_ERROR && totalKb > 0) {
			GLint availKb = 0;
			glGetIntegerv(NVX_AVAILABLE_KB, &availKb);
			if (glGetError() == GL_NO_ERROR) {
				const long long usedKb = static_cast<long long>(totalKb) - static_cast<long long>(availKb);
				return usedKb / 1024;
			}
		}

		// AMD ATI_meminfo: {total, largest free, aux total, aux free} KB. No "total" query exists.
		constexpr GLenum ATI_VBO_FREE_MEMORY = 0x87FB;
		while (glGetError() != GL_NO_ERROR) {}

		GLint freeKb[4] = { 0, 0, 0, 0 };
		glGetIntegerv(ATI_VBO_FREE_MEMORY, freeKb);
		if (glGetError() == GL_NO_ERROR && freeKb[0] > 0) {
			// AMD: no total query; report free as a best-effort approximation.
			return static_cast<long long>(freeKb[0]) / 1024;
		}

		// Intel / Mesa: no standard extension; caller renders "N/A".
		return -1;
	}

} // namespace Axiom

#endif // AXIOM_PROFILER_ENABLED
