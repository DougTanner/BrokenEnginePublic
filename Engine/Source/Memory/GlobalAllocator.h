#pragma once

extern std::atomic<int64_t> giAllocationsThisFrame;

void EnableAllocationTracking(bool bEnable);
