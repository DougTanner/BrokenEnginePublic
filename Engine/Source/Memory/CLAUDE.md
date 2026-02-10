# `/Engine/Source/Memory/`

Global memory allocator and allocation profiling system with dual allocator support: mimalloc (default) or CRT debug heap.

## Overview

Provides custom `operator new`/`operator delete` overloads that route all heap allocations through either mimalloc or the standard CRT allocator, controlled by the compile-time `#define ENABLE_CRT_DEBUG_HEAP` (set in game Pch.h). In the default mimalloc mode, a static initializer (`#pragma init_seg(compiler)`) pre-reserves an 8 GiB arena with eager page commitment to eliminate OS memory calls and soft page faults during gameplay, with debug builds routing mimalloc output to the VS Output window and peak commit logged at exit. In CRT debug heap mode, the standard malloc/free allocators are used with `_CrtSetDbgFlag` leak detection enabled, useful for tracking memory leaks with CRT allocation numbers.

## Key Systems

- **MemoryManager.h** - Header declaring global allocation tracking state (`giAllocationsThisFrame` atomic counter, `giAllocationTrackingSuppressed` thread-local int64_t), `ScopedSuppressAllocationTracking` RAII guard, `EnableAllocationTracking()`, and `ResetAndReportMostCommonAllocation()`. Included by code that needs to suppress allocation tracking or query allocation counts.

- **MemoryManager.cpp** - Global allocator setup, operator new/delete overloads (dual-path: mimalloc or CRT via `#if defined(ENABLE_CRT_DEBUG_HEAP)`), and allocation profiling infrastructure. Every `operator new` call increments an atomic per-frame counter and optionally captures/deduplicates callstacks to identify the most frequent allocation site each frame. Callstack resolution uses StackWalker (DbgHelp) with thread-safety guards (`sResolverMutex` with `try_lock` since DbgHelp is not thread-safe). Profiling is controlled by `kbEnableAllocationTracking` and deferred until `EnableAllocationTracking(true)` is called after the first full frame to skip startup noise.

- **ScopedSuppressAllocationTracking** - RAII guard that increments/decrements `giAllocationTrackingSuppressed` to exclude regions from allocation profiling. Used by Vulkan submission code, collection PreCollision methods, AudioManager, frame creation, and alignment copy operations to filter out expected allocation noise.

## Architecture Notes

The `MemoryInitializer` static initializer runs before `main()` via `#pragma init_seg(compiler)`, ensuring all allocations throughout the process lifetime use the configured allocator. The destructor runs during static destruction after `main()` returns, so it uses `OutputDebugStringA` directly instead of `Log()`.

Allocation tracking uses re-entrancy guards (`sbInAllocationTracker` thread-local) and requires `common::gpThreadLocal` to be initialized, so it naturally excludes allocations from threads without thread-local storage.

`EnableAllocationTracking()` and `ResetAndReportMostCommonAllocation()` are called by Main.cpp and ProfileManagerBase respectively to control the tracking lifecycle.
