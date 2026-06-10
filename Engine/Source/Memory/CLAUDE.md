# `/Engine/Source/Memory/` - Global Allocator & Allocation Tracking

## Overview

Overrides the global C++ `operator new`/`delete` to route through mimalloc (or optionally the CRT debug heap) and hooks every `new` for allocation tracking. No class or `gp*` singleton despite the filename — free functions and operator overloads only; shared by client and server.

## Architecture Notes

- Only the C++ operators live here; the C-level `malloc`/`free` family is overridden by `<mimalloc-override.h>` in `Common/ExternalHeaders.h` (`BT_ENGINE`-gated). Tools builds (DataPacker) compile neither, so they keep the default allocator and suppression is inert there.
- Static initializer configures mimalloc before `main()`: a fixed multi-GiB arena (currently 8 GiB) pre-reserved with eager commit to eliminate OS memory calls and soft page faults during gameplay. `ENABLE_CRT_DEBUG_HEAP` is a compile-time alternative routing through the CRT debug heap for leak detection.
- Counter and debug break are independently gated. The per-frame counter is atomic-relaxed and compiles out unless `kbProfiling`; its per-frame reset and consumption belong to the Profile subsystem ([Profile/CLAUDE.md](../Profile/CLAUDE.md)). The `DEBUG_BREAK` tripwire works even when profiling is disabled. Deletes are never tracked.
- RAII suppression is thread-local — other threads still trip. The suppression counter and RAII type live in [Common/AllocationTracking.h](../../../Common/AllocationTracking.h) so common-layer code can use them; only the allocator here reads the counter.
- Only threads with an initialized `common::gpThreadLocal` participate in tracking; background threads are naturally excluded.
- Debug builds route mimalloc output to VS Output and hook `SIGABRT` into `engine::HandleException` for crash reports. Shutdown reports peak heap and trips `DEBUG_BREAK` if committed memory exceeded the reserve (arena undersized).

See parent [Engine/Source/CLAUDE.md](../CLAUDE.md) "Allocation discipline" for usage rules.
