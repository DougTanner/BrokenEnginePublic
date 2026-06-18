#pragma once

// Read by the Engine allocator's main-loop tracking (Engine/Source/Memory/GlobalAllocator.cpp); tools builds have no allocator override, so the counter is inert there
inline thread_local int64_t giAllocationTrackingSuppressed = 0;

struct ScopedSuppressAllocationTracking
{
	ScopedSuppressAllocationTracking() { ++giAllocationTrackingSuppressed; }
	~ScopedSuppressAllocationTracking() { --giAllocationTrackingSuppressed; }
};

// Temporarily re-arms tracking inside an active ScopedSuppressAllocationTracking scope — e.g. across a
// workbuffer-based callee that is meant to stay armed but whose caller wraps the surrounding work in a
// blanket guard. Construct only while the suppression counter is already positive: it decrements on
// construction (back toward armed) and restores on destruction.
struct ScopedResumeAllocationTracking
{
	ScopedResumeAllocationTracking() { --giAllocationTrackingSuppressed; }
	~ScopedResumeAllocationTracking() { ++giAllocationTrackingSuppressed; }
};
