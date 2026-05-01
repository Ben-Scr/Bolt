#include "pch.hpp"
#include "Time.hpp"
#include <Math/Common.hpp>

namespace Axiom {
	void Time::SetTargetFramerate(float framerate) {
		if (framerate <= 0.0f) {
			m_TargetFPS = 0.0f;
			m_UpdateDeltaTime = std::numeric_limits<float>::infinity();
			m_FrameDuration = std::chrono::steady_clock::duration::max();
			return;
		}

		m_TargetFPS = framerate;
		m_UpdateDeltaTime = 1.0f / m_TargetFPS;
		m_FrameDuration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<float>(m_UpdateDeltaTime)
		);
	}

	void Time::SetTimeScale(float scale) {
		m_TimeScale = std::max(0.0f, scale);
	}

	void Time::SetFixedDeltaTime(float step) {
		step = Clamp(step, 0.f, 1.f);
		m_FixedDeltaTime = step;
	}

	float Time::GetElapsedTime() const {
		std::chrono::duration<float> elapsed = std::chrono::high_resolution_clock::now() - m_StartTime;
		return elapsed.count();
	}

	float Time::GetSimulatedElapsedTime() const { return m_SimulatedElapsedTime; }

	void Time::Update(float deltaTime) {
		m_DeltaTime = deltaTime;
		m_SimulatedElapsedTime += m_DeltaTime * m_TimeScale;
	}
}