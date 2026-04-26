#include "pch.hpp"
#include "Memory.hpp"

#include <atomic>
#include <cstdio>
#include <mutex>

namespace Bolt {

	static Bolt::AllocationStats s_GlobalStats;
	static thread_local bool s_InInit = false;
	static std::atomic<bool> s_IsShutdown = false;
	static std::once_flag s_InitOnce;

	namespace {
		size_t NormalizeAllocationSize(size_t size)
		{
			return size == 0 ? 1 : size;
		}

		void* AllocateOrThrow(size_t size)
		{
			if (void* memory = std::malloc(NormalizeAllocationSize(size))) {
				return memory;
			}

			throw std::bad_alloc();
		}

		struct ScopedInitGuard
		{
			ScopedInitGuard() { s_InInit = true; }
			~ScopedInitGuard() { s_InInit = false; }
		};

#ifndef BT_DIST
		void WriteMemoryReportLine(const char* message)
		{
			std::fputs(message, stderr);

#ifdef BT_PLATFORM_WINDOWS
			OutputDebugStringA(message);
#endif
		}

		void WriteAllocationLeakReport(const AllocatorData& data)
		{
			const auto& allocations = data.m_AllocationMap;
			if (allocations.empty())
				return;

			size_t totalLeaked = 0;
			for (const auto& [memory, allocation] : allocations)
				totalLeaked += allocation.Size;

			char buffer[512];
			std::snprintf(
				buffer,
				sizeof(buffer),
				"[Memory] Detected %zu live allocation(s), %zu byte(s), during allocator shutdown.\n",
				allocations.size(),
				totalLeaked);
			WriteMemoryReportLine(buffer);

			size_t reportedAllocations = 0;
			for (const auto& [memory, allocation] : allocations)
			{
				std::snprintf(
					buffer,
					sizeof(buffer),
					"[Memory]  leak: address=%p size=%zu category=%s\n",
					memory,
					allocation.Size,
					allocation.Category ? allocation.Category : "<uncategorized>");
				WriteMemoryReportLine(buffer);

				if (++reportedAllocations == 32 && reportedAllocations < allocations.size())
				{
					std::snprintf(
						buffer,
						sizeof(buffer),
						"[Memory]  ... %zu additional allocation(s) omitted.\n",
						allocations.size() - reportedAllocations);
					WriteMemoryReportLine(buffer);
					break;
				}
			}
		}
#endif

	}

	void Allocator::Init()
	{
		if (s_Data || s_IsShutdown.load(std::memory_order_acquire))
			return;

		std::call_once(s_InitOnce, []() {
			ScopedInitGuard guard;
			AllocatorData* data = static_cast<AllocatorData*>(Allocator::AllocateRaw(sizeof(AllocatorData)));
			new(data) AllocatorData();
			s_Data = data;
		});
	}

	void Allocator::Shutdown()
	{
		if (!s_Data)
			return;

		ScopedInitGuard guard;
#ifndef BT_DIST
		{
			std::scoped_lock<std::mutex> lock(s_Data->m_Mutex);
			WriteAllocationLeakReport(*s_Data);
		}
#endif
		s_Data->~AllocatorData();
		free(s_Data);
		s_Data = nullptr;
		s_IsShutdown.store(true, std::memory_order_release);
	}

	void* Allocator::AllocateRaw(size_t size)
	{
		return AllocateOrThrow(size);
	}

	void* Allocator::Allocate(size_t size)
	{
		if (s_InInit || s_IsShutdown.load(std::memory_order_acquire))
			return AllocateRaw(size);

		if (!s_Data)
			Init();

		void* memory = AllocateOrThrow(size);

		{
			std::scoped_lock<std::mutex> lock(s_Data->m_Mutex);
			Allocation& alloc = s_Data->m_AllocationMap[memory];
			alloc.Memory = memory;
			alloc.Size = size;

			s_GlobalStats.TotalAllocated += size;
		}

#if HZ_ENABLE_PROFILING
		TracyAlloc(memory, size);
#endif

		return memory;
	}

	void* Allocator::Allocate(size_t size, const char* desc)
	{
		if (s_InInit || s_IsShutdown.load(std::memory_order_acquire))
			return AllocateRaw(size);

		if (!s_Data)
			Init();

		void* memory = AllocateOrThrow(size);

		{
			std::scoped_lock<std::mutex> lock(s_Data->m_Mutex);
			Allocation& alloc = s_Data->m_AllocationMap[memory];
			alloc.Memory = memory;
			alloc.Size = size;
			alloc.Category = desc;

			s_GlobalStats.TotalAllocated += size;
			if (desc)
				s_Data->m_AllocationStatsMap[desc].TotalAllocated += size;
		}

#if HZ_ENABLE_PROFILING
		TracyAlloc(memory, size);
#endif

		return memory;
	}

	void* Allocator::Allocate(size_t size, const char* file, int line)
	{
		if (s_InInit || s_IsShutdown.load(std::memory_order_acquire))
			return AllocateRaw(size);

		if (!s_Data)
			Init();

		void* memory = AllocateOrThrow(size);

		{
			std::scoped_lock<std::mutex> lock(s_Data->m_Mutex);
			Allocation& alloc = s_Data->m_AllocationMap[memory];
			alloc.Memory = memory;
			alloc.Size = size;
			alloc.Category = file;

			s_GlobalStats.TotalAllocated += size;
			s_Data->m_AllocationStatsMap[file].TotalAllocated += size;
		}

#if BT_ENABLE_PROFILING
		TracyAlloc(memory, size);
#endif

		return memory;
	}

	void Allocator::Free(void* memory)
	{
		if (memory == nullptr)
			return;

		if (!s_Data || s_IsShutdown.load(std::memory_order_acquire)) {
			free(memory);
			return;
		}

		{
			bool found = false;
			{
				std::scoped_lock<std::mutex> lock(s_Data->m_Mutex);
				auto allocMapIt = s_Data->m_AllocationMap.find(memory);
				found = allocMapIt != s_Data->m_AllocationMap.end();
				if (found)
				{
					const Allocation& alloc = allocMapIt->second;
					s_GlobalStats.TotalFreed += alloc.Size;
					if (alloc.Category)
						s_Data->m_AllocationStatsMap[alloc.Category].TotalFreed += alloc.Size;

					s_Data->m_AllocationMap.erase(memory);
				}
			}

#if BT_ENABLE_PROFILING
			TracyFree(memory);
#endif

#ifndef BT_DIST
			if (!found)
			{
				//BT_CORE_WARN_TAG("Memory", "Memory block {0} not present in alloc map", memory);
			}
#endif
		}

		free(memory);
	}

	namespace Memory {
		const AllocationStats& GetAllocationStats() { return s_GlobalStats; }
	}
}

#if defined(BT_TRACK_MEMORY) && defined(BT_DEBUG) && defined(BT_PLATFORM_WINDOWS)

_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new(size_t size)
{
	return Bolt::Allocator::Allocate(size);
}

_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new[](size_t size)
{
	return Bolt::Allocator::Allocate(size);
}

_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new(size_t size, const char* desc)
{
	return Bolt::Allocator::Allocate(size, desc);
}

_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new[](size_t size, const char* desc)
{
	return Bolt::Allocator::Allocate(size, desc);
}

_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new(size_t size, const char* file, int line)
{
	return Bolt::Allocator::Allocate(size, file, line);
}

_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new[](size_t size, const char* file, int line)
{
	return Bolt::Allocator::Allocate(size, file, line);
}

void __CRTDECL operator delete(void* memory)
{
	return Bolt::Allocator::Free(memory);
}

void __CRTDECL operator delete(void* memory, const char* desc)
{
	return Bolt::Allocator::Free(memory);
}

void __CRTDECL operator delete(void* memory, const char* file, int line)
{
	return Bolt::Allocator::Free(memory);
}

void __CRTDECL operator delete[](void* memory)
{
	return Bolt::Allocator::Free(memory);
}

void __CRTDECL operator delete[](void* memory, const char* desc)
{
	return Bolt::Allocator::Free(memory);
}

void __CRTDECL operator delete[](void* memory, const char* file, int line)
{
	return Bolt::Allocator::Free(memory);
}

#endif
