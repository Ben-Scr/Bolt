#include "pch.hpp"
#include "Base.hpp"

#include "Core/Log.hpp"
#include "Core/Memory.hpp"

namespace Axiom {

	void InitializeCore()
	{
		Allocator::Init();
		Log::Initialize();

		AIM_CORE_TRACE_TAG("Core", "Axiom Engine {}", AIM_VERSION);
		AIM_CORE_TRACE_TAG("Core", "Initializing...");
	}

	void ShutdownCore()
	{
		AIM_CORE_TRACE_TAG("Core", "Shutting down...");
		Log::Shutdown();
		Allocator::Shutdown();
	}
}
