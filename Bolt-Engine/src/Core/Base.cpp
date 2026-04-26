#include "pch.hpp"
#include "Base.hpp"

#include "Core/Log.hpp"
#include "Core/Memory.hpp"

namespace Bolt {

	void InitializeCore()
	{
		Allocator::Init();
		Log::Initialize();

		BT_CORE_TRACE_TAG("Core", "Bolt Engine {}", BT_VERSION);
		BT_CORE_TRACE_TAG("Core", "Initializing...");
	}

	void ShutdownCore()
	{
		BT_CORE_TRACE_TAG("Core", "Shutting down...");
		Log::Shutdown();
		Allocator::Shutdown();
	}
}
