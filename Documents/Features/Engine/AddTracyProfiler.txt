Add Tracy Profiler (Optional Layer)
====================================

Context
-------
The engine has a comprehensive custom profiling system (85+ CPU timers, 31 GPU timers, boot timers, per-timer allocation tracking, thread-safe, zero-cost when disabled via if constexpr). However it lacks CPU-GPU timeline correlation, lock contention visualization, memory allocation callstacks, and zone drill-down. Tracy (BSD-3) fills these gaps as an additive optional layer behind TRACY_ENABLE, leaving the existing ImGui overlay intact.

Why
---
- CPU-GPU timeline correlation via VK_EXT_calibrated_timestamps
- Lock contention and wait visualization (engine uses mutexes in frame dispatch, profiling, etc.)
- Memory allocation tracking with callstacks (current system counts allocations per-timer but not origin)
- Frame graph with zone drill-down (navigate from frame spikes into zone tree)
- Remote capture to a separate desktop application
- Used by Godot 4, RPCS3, many AAA studios

Changes
-------

1. ThirdParty/
   - Add Tracy as a git submodule (only public/ directory needed: headers + TracyClient.cpp)

2. New file: Common/TracyIntegration.h
   - Include Tracy headers gated behind #ifdef TRACY_ENABLE
   - Define convenience macros (no-op when disabled):
     BT_TRACY_ZONE, BT_TRACY_ZONE_NAME(name), BT_TRACY_FRAME,
     BT_TRACY_THREAD_NAME(name), BT_TRACY_ALLOC(ptr, size), BT_TRACY_FREE(ptr)
   - Add #include "TracyIntegration.h" to Common/ExternalHeaders.h

3. Engine vcxproj (both client and server)
   - Add TracyClient.cpp to project
   - Add TRACY_ENABLE preprocessor define for Profile (and optionally Debug) configurations only
   - Add Tracy include path to additional include directories

4. Common/Threading/Multithreading.cpp
   - Call BT_TRACY_THREAD_NAME() when worker threads are created
   - Name the main thread at startup in Main.cpp

5. Engine/Source/Profile/ProfileManagerBase.h/.cpp
   - Add a tracy::ScopedZone member (or use Tracy C API for dynamic names) to ScopedCpuProfile
   - Tracy zones are stack-scoped, so ScopedCpuProfile RAII lifetime naturally matches
   - All 85+ existing CPU zones appear in Tracy automatically via timer name fields

6. Engine/Source/Main.cpp
   - Add BT_TRACY_FRAME at the top of the main loop iteration (after present, before next frame)

7. Engine/Source/Graphics/ (client only, #ifdef BT_CLIENT)
   - Create TracyVkCtx using TracyVkContextCalibrated() with VkDevice and calibrated timestamps
   - Emit GPU zones in GpuStart()/GpuStop() paths alongside existing timestamp queries
   - Call TracyVkCollect() once per frame to flush GPU results

8. Engine/Source/Memory/GlobalAllocator.cpp
   - In global operator new/delete, emit BT_TRACY_ALLOC/BT_TRACY_FREE
   - Wrap Tracy's own allocations with ScopedSuppressAllocationTracking to prevent recursion
   - Guard all memory hooks behind #ifdef TRACY_ENABLE

Notes
-----
- Tracy spawns a background networking thread -- suppress allocation tracking for it
- TRACY_ENABLE must NOT be defined in Release builds (measurable overhead)
- if constexpr (kbProfiling) guards are independent of TRACY_ENABLE -- both toggle independently
- The existing ImGui overlay remains the primary runtime tool; Tracy is for deep analysis sessions
