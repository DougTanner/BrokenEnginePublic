# `/Engine/Source/Memory/`

Global memory allocator and allocation profiling system with dual allocator support: mimalloc (default) or CRT debug heap.

## Overview

Provides custom `operator new`/`operator delete` overloads that route all heap allocations through either mimalloc or the standard CRT allocator, controlled by the compile-time `#define ENABLE_CRT_DEBUG_HEAP` (set in game Pch.h). In the default mimalloc mode, a static initializer (`#pragma init_seg(compiler)`) pre-reserves an 8 GiB arena with eager page commitment to eliminate OS memory calls and soft page faults during gameplay, with debug builds routing mimalloc output to the VS Output window and peak usage/committed stats logged at exit via `mi_stats_get()`. In CRT debug heap mode, the standard malloc/free allocators are used with `_CrtSetDbgFlag` leak detection enabled, useful for tracking memory leaks with CRT allocation numbers.

## Key Systems

- **MemoryManager.h** - Header declaring global allocation tracking state (`giAllocationsThisFrame` atomic counter, `giAllocationTrackingSuppressed` thread-local int64_t), `ScopedSuppressAllocationTracking` RAII guard, and `EnableAllocationTracking()`.

- **MemoryManager.cpp** - Global allocator setup, operator new/delete overloads (dual-path: mimalloc or CRT via `#if defined(ENABLE_CRT_DEBUG_HEAP)`), and allocation tracking. Every `operator new` call increments an atomic per-frame counter (`giAllocationsThisFrame`) when profiling is enabled. Once tracking is active (via `EnableAllocationTracking(true)`), allocations on threads with initialized `ThreadLocal` trigger `DebugBreak()` to catch unexpected heap allocations during the main loop. `ScopedSuppressAllocationTracking` and `giAllocationTrackingSuppressed` allow regions to opt out of the debug break.

- **ScopedSuppressAllocationTracking** - RAII guard that increments/decrements `giAllocationTrackingSuppressed` to exclude regions from allocation tracking. Used by code that legitimately allocates during the main loop.

## Architecture Notes

The `MemoryInitializer` static initializer runs before `main()` via `#pragma init_seg(compiler)`, ensuring all allocations throughout the process lifetime use the configured allocator. The destructor runs during static destruction after `main()` returns, so it uses `OutputDebugStringA` directly instead of `Log()`. At shutdown, it merges per-thread mimalloc stats and reports peak heap usage and peak committed memory.

Allocation tracking requires `common::gpThreadLocal` to be initialized, so it naturally excludes allocations from threads without thread-local storage. `EnableAllocationTracking()` is called by Main.cpp to control when tracking becomes active.
