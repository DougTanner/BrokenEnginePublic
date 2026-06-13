#pragma once

// Read by the Engine allocator's main-loop tracking (Engine/Source/Memory/GlobalAllocator.cpp); tools builds have no allocator override, so the counter is inert there
inline thread_local int64_t giAllocationTrackingSuppressed = 0;

struct ScopedSuppressAllocationTracking
{
	ScopedSuppressAllocationTracking() { ++giAllocationTrackingSuppressed; }
	~ScopedSuppressAllocationTracking() { --giAllocationTrackingSuppressed; }
};
