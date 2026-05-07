#include "pch.hpp"
#include "Memory.hpp"

#include <atomic>
#include <cstdio>
#include <mutex>

namespace Axiom {

	static Axiom::AllocationStats s_GlobalStats;
	// Dedicated stats mutex so the diagnostic snapshot accessor doesn't have to
	// hand out the per-allocator lock (which is owned by AllocatorData) or risk
	// reading torn size_t pairs on platforms where the pair isn't atomic.
	static std::mutex s_GlobalStatsMutex;
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

#ifndef AIM_DIST
		void WriteMemoryReportLine(const char* message)
		{
			std::fputs(message, stderr);

#ifdef AIM_PLATFORM_WINDOWS
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

			constexpr size_t k_MaxReportedLeaks = 32;
			size_t reportedAllocations = 0;
			for (const auto& [memory, allocation] : allocations)
			{
				if (reportedAllocations >= k_MaxReportedLeaks)
					break;

				std::snprintf(
					buffer,
					sizeof(buffer),
					"[Memory]  leak: address=%p size=%zu category=%s\n",
					memory,
					allocation.Size,
					allocation.Category ? allocation.Category : "<uncategorized>");
				WriteMemoryReportLine(buffer);
				++reportedAllocations;
			}

			if (allocations.size() > k_MaxReportedLeaks)
			{
				std::snprintf(
					buffer,
					sizeof(buffer),
					"[Memory]  ... showing first %zu of %zu total leaked allocation(s).\n",
					k_MaxReportedLeaks,
					allocations.size());
				WriteMemoryReportLine(buffer);
			}
		}
#endif

	}

	void Allocator::Init()
	{
		if (s_Data.load(std::memory_order_acquire) || s_IsShutdown.load(std::memory_order_acquire))
			return;

		std::call_once(s_InitOnce, []() {
			ScopedInitGuard guard;
			AllocatorData* data = static_cast<AllocatorData*>(Allocator::AllocateRaw(sizeof(AllocatorData)));
			new(data) AllocatorData();
			s_Data.store(data, std::memory_order_release);
		});
	}

	void Allocator::Shutdown()
	{
		AllocatorData* data = s_Data.load(std::memory_order_acquire);
		if (!data)
			return;

		ScopedInitGuard guard;
#ifndef AIM_DIST
		{
			std::scoped_lock<std::mutex> lock(data->m_Mutex);
			WriteAllocationLeakReport(*data);
		}
#endif
		data->~AllocatorData();
		free(data);
		s_Data.store(nullptr, std::memory_order_release);
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

		if (!s_Data.load(std::memory_order_acquire))
			Init();

		AllocatorData* data = s_Data.load(std::memory_order_acquire);
		void* memory = AllocateOrThrow(size);

		{
			std::scoped_lock<std::mutex> lock(data->m_Mutex);
			Allocation& alloc = data->m_AllocationMap[memory];
			alloc.Memory = memory;
			alloc.Size = size;
		}

		// s_GlobalStats has its own mutex so the diagnostic reader can take a
		// consistent snapshot without contending the per-allocator data lock.
		{
			std::scoped_lock<std::mutex> statsLock(s_GlobalStatsMutex);
			s_GlobalStats.TotalAllocated += size;
		}

#if AIM_ENABLE_PROFILING
		TracyAlloc(memory, size);
#endif

		return memory;
	}

	void* Allocator::Allocate(size_t size, const char* desc)
	{
		if (s_InInit || s_IsShutdown.load(std::memory_order_acquire))
			return AllocateRaw(size);

		if (!s_Data.load(std::memory_order_acquire))
			Init();

		AllocatorData* data = s_Data.load(std::memory_order_acquire);
		void* memory = AllocateOrThrow(size);

		{
			std::scoped_lock<std::mutex> lock(data->m_Mutex);
			Allocation& alloc = data->m_AllocationMap[memory];
			alloc.Memory = memory;
			alloc.Size = size;
			alloc.Category = desc;

			if (desc)
				data->m_AllocationStatsMap[desc].TotalAllocated += size;
		}

		{
			std::scoped_lock<std::mutex> statsLock(s_GlobalStatsMutex);
			s_GlobalStats.TotalAllocated += size;
		}

#if AIM_ENABLE_PROFILING
		TracyAlloc(memory, size);
#endif

		return memory;
	}

	void* Allocator::Allocate(size_t size, const char* file, int line)
	{
		if (s_InInit || s_IsShutdown.load(std::memory_order_acquire))
			return AllocateRaw(size);

		if (!s_Data.load(std::memory_order_acquire))
			Init();

		AllocatorData* data = s_Data.load(std::memory_order_acquire);
		void* memory = AllocateOrThrow(size);

		{
			std::scoped_lock<std::mutex> lock(data->m_Mutex);
			Allocation& alloc = data->m_AllocationMap[memory];
			alloc.Memory = memory;
			alloc.Size = size;
			alloc.Category = file;

			data->m_AllocationStatsMap[file].TotalAllocated += size;
		}

		{
			std::scoped_lock<std::mutex> statsLock(s_GlobalStatsMutex);
			s_GlobalStats.TotalAllocated += size;
		}

#if AIM_ENABLE_PROFILING
		TracyAlloc(memory, size);
#endif

		return memory;
	}

	void Allocator::Free(void* memory)
	{
		if (memory == nullptr)
			return;

		AllocatorData* data = s_Data.load(std::memory_order_acquire);
		if (!data || s_IsShutdown.load(std::memory_order_acquire)) {
			free(memory);
			return;
		}

		{
			bool found = false;
			size_t freedSize = 0;
			{
				std::scoped_lock<std::mutex> lock(data->m_Mutex);
				auto allocMapIt = data->m_AllocationMap.find(memory);
				found = allocMapIt != data->m_AllocationMap.end();
				if (found)
				{
					const Allocation& alloc = allocMapIt->second;
					freedSize = alloc.Size;
					if (alloc.Category)
						data->m_AllocationStatsMap[alloc.Category].TotalFreed += alloc.Size;

					data->m_AllocationMap.erase(memory);
				}
			}

			// Update the global counter under its own mutex so readers see a
			// consistent (TotalAllocated, TotalFreed) pair.
			if (found) {
				std::scoped_lock<std::mutex> statsLock(s_GlobalStatsMutex);
				s_GlobalStats.TotalFreed += freedSize;
			}

#if AIM_ENABLE_PROFILING
			TracyFree(memory);
#endif

#ifndef AIM_DIST
			if (!found)
			{
				//AIM_CORE_WARN_TAG("Memory", "Memory block {0} not present in alloc map", memory);
			}
#endif
		}

		free(memory);
	}

	std::map<std::string, AllocationStats> Allocator::GetAllocationStats()
	{
		std::map<std::string, AllocationStats> result;
		AllocatorData* data = s_Data.load(std::memory_order_acquire);
		if (!data)
			return result;

		std::scoped_lock<std::mutex> lock(data->m_Mutex);
		for (const auto& [category, stats] : data->m_AllocationStatsMap) {
			result.emplace(category ? category : "<uncategorized>", stats);
		}
		return result;
	}

	namespace Memory {
		const AllocationStats& GetAllocationStats() { return s_GlobalStats; }

		// Snapshot accessor: take the stats lock so the (TotalAllocated, TotalFreed)
		// pair is read atomically. The legacy reference accessor above can race
		// with concurrent allocations and is kept only for source compatibility.
		AllocationStatsSnapshot GetAllocationStatsSnapshot() {
			std::scoped_lock<std::mutex> lock(s_GlobalStatsMutex);
			AllocationStatsSnapshot snapshot;
			snapshot.TotalAllocated = s_GlobalStats.TotalAllocated;
			snapshot.TotalFreed = s_GlobalStats.TotalFreed;
			return snapshot;
		}
	}
}

#if defined(AIM_TRACK_MEMORY) && defined(AIM_DEBUG) && defined(AIM_PLATFORM_WINDOWS)

_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new(size_t size)
{
	return Axiom::Allocator::Allocate(size);
}

_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new[](size_t size)
{
	return Axiom::Allocator::Allocate(size);
}

_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new(size_t size, const char* desc)
{
	return Axiom::Allocator::Allocate(size, desc);
}

_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new[](size_t size, const char* desc)
{
	return Axiom::Allocator::Allocate(size, desc);
}

_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new(size_t size, const char* file, int line)
{
	return Axiom::Allocator::Allocate(size, file, line);
}

_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new[](size_t size, const char* file, int line)
{
	return Axiom::Allocator::Allocate(size, file, line);
}

void __CRTDECL operator delete(void* memory)
{
	return Axiom::Allocator::Free(memory);
}

void __CRTDECL operator delete(void* memory, const char* desc)
{
	return Axiom::Allocator::Free(memory);
}

void __CRTDECL operator delete(void* memory, const char* file, int line)
{
	return Axiom::Allocator::Free(memory);
}

void __CRTDECL operator delete[](void* memory)
{
	return Axiom::Allocator::Free(memory);
}

void __CRTDECL operator delete[](void* memory, const char* desc)
{
	return Axiom::Allocator::Free(memory);
}

void __CRTDECL operator delete[](void* memory, const char* file, int line)
{
	return Axiom::Allocator::Free(memory);
}

#endif
