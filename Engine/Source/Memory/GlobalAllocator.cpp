#include "GlobalAllocator.h"

#include "CrashReport.h"

std::atomic<int64_t> giAllocationsThisFrame = 0;

namespace
{

std::atomic<bool> sbTrackingReady = false;

void TrackAllocation()
{
	if constexpr (kbProfiling)
	{
		giAllocationsThisFrame.fetch_add(1, std::memory_order_relaxed);
	}

	if (!sbTrackingReady.load(std::memory_order_relaxed) || common::gpThreadLocal == nullptr || giAllocationTrackingSuppressed > 0)
	{
		return;
	}

	// Heap allocation during main loop, use Workbuffer or filter out with ScopedSuppressAllocationTracking
	DEBUG_BREAK();
}

} // namespace

void EnableAllocationTracking(bool bEnable)
{
	sbTrackingReady.store(bEnable, std::memory_order_relaxed);
}

#if defined(ENABLE_CRT_DEBUG_HEAP)

[[nodiscard]] _Ret_notnull_ _Post_writable_byte_size_(n) void* operator new(std::size_t n) noexcept(false) { TrackAllocation(); void* p = std::malloc(n); __assume(p); return p; }
[[nodiscard]] _Ret_notnull_ _Post_writable_byte_size_(n) void* operator new[](std::size_t n) noexcept(false) { TrackAllocation(); void* p = std::malloc(n); __assume(p); return p; }
[[nodiscard]] _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n) void* operator new  (std::size_t n, const std::nothrow_t&) noexcept { TrackAllocation(); return std::malloc(n); }
[[nodiscard]] _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n) void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { TrackAllocation(); return std::malloc(n); }
[[nodiscard]] _Ret_notnull_ _Post_writable_byte_size_(n) void* operator new  (std::size_t n, std::align_val_t al) noexcept(false) { TrackAllocation(); void* p = _aligned_malloc(n, static_cast<size_t>(al)); __assume(p); return p; }
[[nodiscard]] _Ret_notnull_ _Post_writable_byte_size_(n) void* operator new[](std::size_t n, std::align_val_t al) noexcept(false) { TrackAllocation(); void* p = _aligned_malloc(n, static_cast<size_t>(al)); __assume(p); return p; }
[[nodiscard]] _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n) void* operator new  (std::size_t n, std::align_val_t al, const std::nothrow_t&) noexcept { TrackAllocation(); return _aligned_malloc(n, static_cast<size_t>(al)); }
[[nodiscard]] _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n) void* operator new[](std::size_t n, std::align_val_t al, const std::nothrow_t&) noexcept { TrackAllocation(); return _aligned_malloc(n, static_cast<size_t>(al)); }

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete  (void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete  (void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete  (void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete  (void* p, std::size_t, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete  (void* p, std::align_val_t, const std::nothrow_t&) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { _aligned_free(p); }

#else

// Hand-maintained fork of ThirdParty/mimalloc/include/mimalloc-new-delete.h with TrackAllocation() injected
// (the stock header has no hook point) - diff against the stock header on each mimalloc upgrade
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

constexpr int64_t kiMimallocArenaReserveMb = 10 * 1024;

struct MemoryInitializer
{
	MemoryInitializer()
	{
#if defined(ENABLE_CRT_DEBUG_HEAP)
		_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
		// Usage: set to the allocation number from the CRT leak report to break on that allocation (e.g., _crtBreakAlloc = 5374)
		// _crtBreakAlloc = 5374;
#else
		// Pre-commit arena pages on allocation (eliminates soft page faults during gameplay)
		mi_option_set(mi_option_arena_eager_commit, 1);

		// Pre-reserve a large arena at startup (eliminates OS memory calls during gameplay)
		mi_option_set(mi_option_reserve_os_memory, kiMimallocArenaReserveMb * 1024);

#if defined(DEBUG) || defined(_DEBUG)
		// Route mimalloc output to VS Output window
		mi_register_output([](const char* msg, [[maybe_unused]] void* arg) { OutputDebugStringA(msg); }, nullptr);

		// Write crash report on any abort() call (catches mimalloc assertions and other CRT aborts)
		signal(SIGABRT, [](int) { engine::HandleException(); });
#endif
#endif
	}

	~MemoryInitializer()
	{
#if !defined(ENABLE_CRT_DEBUG_HEAP) && (defined(DEBUG) || defined(_DEBUG))
		mi_stats_merge();

		mi_stats_t stats = {};
		stats.size = sizeof(mi_stats_t);
		stats.version = MI_STAT_VERSION;
		mi_stats_get(&stats);

		int64_t iPeakCommittedMb = stats.committed.peak / (1024 * 1024);

		LOG(kDefault, kInfo, "Mimalloc peak heap usage: {} MiB, peak committed: {} MiB (arena reserve: {} MiB)", stats.page_committed.peak / (1024 * 1024), iPeakCommittedMb, kiMimallocArenaReserveMb);

		if (iPeakCommittedMb > kiMimallocArenaReserveMb)
		{
			DEBUG_BREAK();
		}
#endif
	}
};

MemoryInitializer gMemoryInitializer;
