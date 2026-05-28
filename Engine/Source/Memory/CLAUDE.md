# `/Engine/Source/Memory/` - Global Allocator & Allocation Tracking

## Overview

Replaces the default C++ allocator with mimalloc (or optionally the CRT debug heap) via `operator new`/`delete` overloads, and tracks per-frame heap allocations to catch unintended main-loop allocations.

## Architecture Notes

- Static initializer configures the allocator before `main()`; mimalloc pre-reserves a fixed multi-GiB arena (currently 8 GiB) with eager commit to eliminate OS calls and soft page faults during gameplay. `ENABLE_CRT_DEBUG_HEAP` is a compile-time alternative routing through the CRT debug heap for leak detection.
- Per-frame allocation counter is atomic-relaxed and profiling-only. The tracking debug break is gated open only between the explicit enable/disable bracketing the main loop, so startup/teardown allocate freely. RAII suppression is thread-local — other threads still trip.
- Only threads with an initialized `common::gpThreadLocal` participate in tracking; background threads are naturally excluded.
- Debug builds route mimalloc output to VS Output and hook `SIGABRT` into `engine::HandleException` for crash reports. Shutdown reports peak heap and trips `DEBUG_BREAK` if committed memory exceeded the reserve (arena undersized).

See parent [Engine/Source/CLAUDE.md](../CLAUDE.md) "Allocation discipline" for usage rules.
