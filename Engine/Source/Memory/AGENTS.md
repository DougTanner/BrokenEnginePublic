# `/Engine/Source/Memory/` - Global Allocator & Allocation Tracking

## Overview

Overrides the global C++ `operator new`/`delete` to route through mimalloc (or optionally the CRT debug heap) and hooks every `new` for allocation tracking (`GlobalAllocator.{h,cpp}`). No class or `gp*` singleton — free functions and operator overloads only; shared by client and server.

## Architecture Notes

- Only the C++ operators live here; the C-level `malloc`/`free` family is overridden by `<mimalloc-override.h>` in `Common/ExternalHeaders.h` (`BT_ENGINE`-gated). Tools builds (DataPacker) compile neither, so they keep the default allocator and suppression is inert there.
- The non-debug-heap operator block is a hand-maintained fork of `ThirdParty/mimalloc/include/mimalloc-new-delete.h` with `TrackAllocation()` injected (the stock header has no hook point) — diff against the stock header on each mimalloc upgrade.
- Static initializer configures mimalloc before `main()`: a fixed 10 GiB arena pre-reserved with eager commit to eliminate OS memory calls and soft page faults during gameplay. `ENABLE_CRT_DEBUG_HEAP` is a compile-time alternative routing through the CRT debug heap for leak detection.
- Counter and debug break are independently gated. The main-loop enable flag and the per-frame counter are both atomic-relaxed (main thread toggles the flag while allocating threads read it; relaxed suffices — tracking is a best-effort tripwire, not a synchronization point); the counter compiles out unless `kbProfiling`; its per-frame reset and consumption belong to the Profile subsystem (`../Profile/AGENTS.md`). The `DEBUG_BREAK` tripwire works even when profiling is disabled. Deletes are never tracked.
- RAII suppression is thread-local — other threads still trip. The suppression counter and RAII type live in `../../../Common/AllocationTracking.h` so common-layer code can use them; only the allocator here reads the counter.
- Only threads with an initialized `common::gpThreadLocal` participate in tracking; engine worker threads construct one and participate, while threads that never do (OS/third-party) are naturally excluded. `std::async` tasks are not inherently excluded — the engine's DxDiag task constructs a `ThreadLocal` (`CrashReport.cpp`) and participates.
- Debug builds route mimalloc output to VS Output and hook `SIGABRT` into `engine::HandleException` for crash reports. Shutdown reports peak heap and trips `DEBUG_BREAK` if committed memory exceeded the reserve (arena undersized).

The main-loop allocation boundary is defined at the Engine Source hub.
