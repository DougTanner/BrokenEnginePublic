#include "MemoryManager.h"

#include "ErrorUtils.h"
#include "ThreadLocal.h"

std::atomic<int64_t> giAllocationsThisFrame = 0;
thread_local int64_t giAllocationTrackingSuppressed = 0;

namespace
{

bool sbTrackingReady = false;

void TrackAllocation()
{
	if (kbEnableProfiling)
	{
		giAllocationsThisFrame.fetch_add(1, std::memory_order_relaxed);
	}

	if (!sbTrackingReady || common::gpThreadLocal == nullptr || giAllocationTrackingSuppressed > 0)
	{
		return;
	}

	// Heap allocation during main loop, use Workbuffer or filter out with ScopedSuppressAllocationTracking
	common::DebugBreak();
}

} // namespace

void EnableAllocationTracking(bool bEnable)
{
	sbTrackingReady = bEnable;
}

#if defined(ENABLE_CRT_DEBUG_HEAP)

[[nodiscard]] _Ret_notnull_ _Post_writable_byte_size_(n) void* operator new(std::size_t n) noexcept(false) { TrackAllocation(); void* p = malloc(n); __assume(p); return p; }
[[nodiscard]] _Ret_notnull_ _Post_writable_byte_size_(n) void* operator new[](std::size_t n) noexcept(false) { TrackAllocation(); void* p = malloc(n); __assume(p); return p; }
[[nodiscard]] _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n) void* operator new  (std::size_t n, const std::nothrow_t&) noexcept { TrackAllocation(); return malloc(n); }
[[nodiscard]] _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n) void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { TrackAllocation(); return malloc(n); }
[[nodiscard]] _Ret_notnull_ _Post_writable_byte_size_(n) void* operator new  (std::size_t n, std::align_val_t al) noexcept(false) { TrackAllocation(); void* p = _aligned_malloc(n, static_cast<size_t>(al)); __assume(p); return p; }
[[nodiscard]] _Ret_notnull_ _Post_writable_byte_size_(n) void* operator new[](std::size_t n, std::align_val_t al) noexcept(false) { TrackAllocation(); void* p = _aligned_malloc(n, static_cast<size_t>(al)); __assume(p); return p; }
[[nodiscard]] _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n) void* operator new  (std::size_t n, std::align_val_t al, const std::nothrow_t&) noexcept { TrackAllocation(); return _aligned_malloc(n, static_cast<size_t>(al)); }
[[nodiscard]] _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n) void* operator new[](std::size_t n, std::align_val_t al, const std::nothrow_t&) noexcept { TrackAllocation(); return _aligned_malloc(n, static_cast<size_t>(al)); }

void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete  (void* p, const std::nothrow_t&) noexcept { free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { free(p); }
void operator delete  (void* p, std::size_t) noexcept { free(p); }
void operator delete[](void* p, std::size_t) noexcept { free(p); }
void operator delete  (void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete  (void* p, std::size_t, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete  (void* p, std::align_val_t, const std::nothrow_t&) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { _aligned_free(p); }

#else

[[nodiscard]] _Ret_notnull_ _Post_writable_byte_size_(n) void* operator new(std::size_t n) noexcept(false) { TrackAllocation(); return mi_new(n); }
[[nodiscard]] _Ret_notnull_ _Post_writable_byte_size_(n) void* operator new[](std::size_t n) noexcept(false) { TrackAllocation(); return mi_new(n); }
[[nodiscard]] _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n) void* operator new  (std::size_t n, const std::nothrow_t&) noexcept { TrackAllocation(); return mi_new_nothrow(n); }
[[nodiscard]] _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n) void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { TrackAllocation(); return mi_new_nothrow(n); }
[[nodiscard]] _Ret_notnull_ _Post_writable_byte_size_(n) void* operator new  (std::size_t n, std::align_val_t al) noexcept(false) { TrackAllocation(); return mi_new_aligned(n, static_cast<size_t>(al)); }
[[nodiscard]] _Ret_notnull_ _Post_writable_byte_size_(n) void* operator new[](std::size_t n, std::align_val_t al) noexcept(false) { TrackAllocation(); return mi_new_aligned(n, static_cast<size_t>(al)); }
[[nodiscard]] _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n) void* operator new  (std::size_t n, std::align_val_t al, const std::nothrow_t&) noexcept { TrackAllocation(); return mi_new_aligned_nothrow(n, static_cast<size_t>(al)); }
[[nodiscard]] _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n) void* operator new[](std::size_t n, std::align_val_t al, const std::nothrow_t&) noexcept { TrackAllocation(); return mi_new_aligned_nothrow(n, static_cast<size_t>(al)); }

void operator delete(void* p) noexcept { mi_free(p); }
void operator delete[](void* p) noexcept { mi_free(p); }
void operator delete  (void* p, const std::nothrow_t&) noexcept { mi_free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { mi_free(p); }
void operator delete  (void* p, std::size_t n) noexcept { mi_free_size(p, n); }
void operator delete[](void* p, std::size_t n) noexcept { mi_free_size(p, n); }
void operator delete  (void* p, std::align_val_t al) noexcept { mi_free_aligned(p, static_cast<size_t>(al)); }
void operator delete[](void* p, std::align_val_t al) noexcept { mi_free_aligned(p, static_cast<size_t>(al)); }
void operator delete  (void* p, std::size_t n, std::align_val_t al) noexcept { mi_free_size_aligned(p, n, static_cast<size_t>(al)); }
void operator delete[](void* p, std::size_t n, std::align_val_t al) noexcept { mi_free_size_aligned(p, n, static_cast<size_t>(al)); }
void operator delete  (void* p, std::align_val_t al, const std::nothrow_t&) noexcept { mi_free_aligned(p, static_cast<size_t>(al)); }
void operator delete[](void* p, std::align_val_t al, const std::nothrow_t&) noexcept { mi_free_aligned(p, static_cast<size_t>(al)); }

#endif

constexpr long kiMimallocArenaReserveMb = 3 * 1024;

struct MemoryInitializer
{
	MemoryInitializer()
	{
#if defined(ENABLE_CRT_DEBUG_HEAP)
		_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
		// Set to the allocation number from the CRT leak report to break on that allocation
		// _crtBreakAlloc = 5374;
#else
		// Pre-commit arena pages on allocation (eliminates soft page faults during gameplay)
		mi_option_set(mi_option_arena_eager_commit, 1);

		// Pre-reserve a large arena at startup (eliminates OS memory calls during gameplay)
		mi_option_set(mi_option_reserve_os_memory, kiMimallocArenaReserveMb * 1024L);

#if defined(DEBUG) || defined(_DEBUG)
		// Route mimalloc output to VS Output window
		mi_register_output([](const char* msg, [[maybe_unused]] void* arg) { OutputDebugStringA(msg); }, nullptr);
#endif
#endif
	}

	~MemoryInitializer()
	{
#if !defined(ENABLE_CRT_DEBUG_HEAP)
		mi_stats_merge();

		mi_stats_t stats = {};
		stats.size = sizeof(mi_stats_t);
		stats.version = MI_STAT_VERSION;
		mi_stats_get(&stats);

		int64_t iPeakUsageMb = stats.page_committed.peak / (1024 * 1024);
		int64_t iPeakCommittedMb = stats.committed.peak / (1024 * 1024);

		char pcBuffer[256];
		snprintf(pcBuffer, sizeof(pcBuffer), "[mimalloc] Peak heap usage: %lld MiB, peak committed: %lld MiB (arena reserve: %ld MiB)\n", iPeakUsageMb, iPeakCommittedMb, kiMimallocArenaReserveMb);
		OutputDebugStringA(pcBuffer);

		if (iPeakCommittedMb > kiMimallocArenaReserveMb)
		{
			common::DebugBreak();
		}
#endif
	}
};

MemoryInitializer gMemoryInitializer;
