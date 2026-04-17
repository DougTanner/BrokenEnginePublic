# `/Engine/Source/Memory/` - Global Allocator & Allocation Tracking

## Overview

Replaces the default C++ allocator with mimalloc (or optionally the CRT debug heap) via `operator new`/`delete` overloads, and tracks per-frame heap allocations to catch unintended main-loop allocations.

## Architecture Notes

- Static initializer configures the allocator before `main()`; mimalloc pre-reserves a multi-GiB arena with eager commit to eliminate OS calls and soft page faults during gameplay. CRT debug heap is a compile-time alternative for leak detection.
- Per-frame allocation counter is atomic-relaxed, profiling-only; the tracking debug break is armed globally around the main loop so startup/teardown allocate freely. RAII suppression is thread-local — other threads still trip.
- Only threads with initialized `ThreadLocal` participate in tracking; background threads are naturally excluded.
- Debug builds route mimalloc output to VS Output and hook `SIGABRT` into `engine::HandleException` for crash reports. Shutdown reports peak heap and trips `DEBUG_BREAK` if committed memory exceeded the reserve (arena undersized).

See parent [Engine/Source/CLAUDE.md](../CLAUDE.md) "Allocation discipline" for usage rules.
