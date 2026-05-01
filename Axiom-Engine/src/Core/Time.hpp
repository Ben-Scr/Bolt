#pragma once
#include <chrono>
#include "Core/Export.hpp"

namespace Axiom {
	class Application;

	class AXIOM_API Time {
	public:
		float GetDeltaTime() const { return m_DeltaTime * m_TimeScale; }
		float GetFrameRate() const { return m_DeltaTime > 0.0f ? 1.0f / m_DeltaTime : 0.0f; }
		float GetDeltaTimeUnscaled() const { return m_DeltaTime; }
		void SetTargetFramerate(float fps);

		float GetFixedDeltaTime() const { return m_FixedDeltaTime * m_TimeScale; }
		void SetFixedDeltaTime(float step);

		float GetUnscaledFixedDeltaTime() const { return m_FixedDeltaTime; }

		float GetTimeScale() const { return m_TimeScale; }
		void SetTimeScale(float scale);

		// Note: Realtime elapsed time
		float GetElapsedTime() const;
		// Note: Elapsed time based on timescale
		float GetSimulatedElapsedTime() const;

		int GetFrameCount() const { return m_FrameCount; }

	private:
		void Update(float deltaTime);
		void AdvanceFrameCount() { m_FrameCount++; }

		float m_DeltaTime = 0.0f;
		float m_TargetFPS = 144.f;
		float m_TimeScale = 1.f;
		float m_UpdateDeltaTime = 1.0f / m_TargetFPS;
		float m_FixedDeltaTime = 1.0f / 50.f;
		float m_SimulatedElapsedTime = 0.0f;
		int m_FrameCount = 0;

		std::chrono::steady_clock::duration m_FrameDuration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<float>(1.0f / m_TargetFPS)
		);
		std::chrono::high_resolution_clock::time_point m_StartTime = std::chrono::high_resolution_clock::now();

		friend class Application;
	};
}