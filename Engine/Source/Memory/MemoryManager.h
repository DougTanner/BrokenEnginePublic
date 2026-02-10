#pragma once

extern std::atomic<int64_t> giAllocationsThisFrame;
extern thread_local int64_t giAllocationTrackingSuppressed;

struct ScopedSuppressAllocationTracking
{
	ScopedSuppressAllocationTracking() { ++giAllocationTrackingSuppressed; }
	~ScopedSuppressAllocationTracking() { --giAllocationTrackingSuppressed; }
};

void EnableAllocationTracking(bool bEnable);
