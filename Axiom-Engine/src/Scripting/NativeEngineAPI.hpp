#pragma once

#include <cstdint>

namespace Axiom {

	struct NativeEngineAPI {
		void (*LogInfo)(const char* msg);
		void (*LogWarn)(const char* msg);
		void (*LogError)(const char* msg);
		float (*GetDeltaTime)();
		void (*GetPosition)(uint32_t entity, float* x, float* y);
		void (*SetPosition)(uint32_t entity, float x, float y);
		float (*GetRotation)(uint32_t entity);
		void (*SetRotation)(uint32_t entity, float rot);
	};

	inline NativeEngineAPI* g_EngineAPI = nullptr;

} // namespace Axiom
