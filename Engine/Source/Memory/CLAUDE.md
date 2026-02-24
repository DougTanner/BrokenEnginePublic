# `/Engine/Source/Memory/`

Global memory allocator and allocation tracking system.

## Overview

Replaces the default C++ allocator with mimalloc (or optionally the CRT debug heap) and provides per-frame allocation tracking that catches unexpected heap allocations during gameplay. All heap allocations in the process route through custom `operator new`/`operator delete` overloads.

## Key Systems

- **MemoryManager** - Dual-mode allocator selected at compile time: mimalloc (default) pre-reserves a large arena at startup to eliminate OS memory calls and page faults during gameplay; CRT debug heap mode (`ENABLE_CRT_DEBUG_HEAP`) enables leak detection with allocation-number breakpoints. Every allocation increments a per-frame counter for profiling and optionally triggers a debug break to catch unintended heap usage in the main loop.

- **ScopedSuppressAllocationTracking** - RAII guard that suppresses allocation tracking debug breaks for code regions where heap allocation is intentional. Usage pattern documented in root CLAUDE.md under "Allocation tracking".

## Architecture Notes

A static initializer constructed before `main()` configures the allocator for the entire process lifetime. At shutdown, peak heap usage stats are reported to the VS Output window (mimalloc mode only). Allocation tracking only activates on threads with initialized `ThreadLocal`, so background threads without thread-local storage are naturally excluded.
