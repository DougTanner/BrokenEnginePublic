# Architecture: Allocation-Tracking Flag Atomicity & Allocator Doc Notes

## Context

Source: /external-deep-analysis on `Engine/Source/Memory`. `sbTrackingReady` (`MemoryManager.cpp:10`) is a
plain `bool` written by the main thread via `EnableAllocationTracking` (`Main.cpp:256` true / `:317` false)
while every allocating thread reads it inside `TrackAllocation` (`MemoryManager.cpp:19`). At the `:256` write,
tracking-eligible threads (those with a `common::gpThreadLocal`) are already live: Multithreading workers
(`Main.cpp:85`), the TextureUploadManager thread (`TextureUploadManager.cpp:151`), FileManager load threads
(`FileManager.cpp:318/:463`), and the DxDiag `std::async` task (`CrashReport.cpp:81` constructs a
`ThreadLocal`). Formal data race (UB) — benign by sequencing today (stale `false` only delays tracking onset;
stale `true` post-`:317` could at worst fire a debugger-only `DEBUG_BREAK` in a teardown window). The fix is
free; the same review surfaced three small doc-accuracy items.

## Design

### Engine/Source/Memory/MemoryManager.cpp
- Make `sbTrackingReady` a `std::atomic<bool>` — relaxed load in `TrackAllocation` (`:19`), relaxed store in
  `EnableAllocationTracking` (`:32`) — mirroring `giAllocationsThisFrame`'s relaxed discipline two lines above
  (`:16`). Relaxed atomic bool load compiles to a plain load on x64: zero hot-path cost [~5m]
- Add a maintenance comment above the non-debug-heap operator block (`:61`): these forwarders are a
  hand-maintained fork of `ThirdParty/mimalloc/include/mimalloc-new-delete.h` with `TrackAllocation()`
  injected (the stock header has no hook point) — diff against the stock header on each mimalloc upgrade [~5m]

### Common/ExternalHeaders.h
- Add `<new>` and `<cstdlib>` to the std-library block (`:59-102`): `std::align_val_t`/`std::nothrow_t` and
  the `malloc`/`free` family used by `MemoryManager.cpp:37-81` currently arrive only via MSVC-internal
  transitive includes — the one std vocabulary the explicit-ownership policy doesn't cover [~5m]

### Engine/Source/Memory/CLAUDE.md
- Fix the participation example (`:13`): "bare `std::async` tasks" are listed as naturally excluded from
  tracking, but the engine's own DxDiag `std::async` task constructs a `ThreadLocal` (`CrashReport.cpp:81`)
  and therefore participates — reword so the example doesn't mislead someone reasoning about DxDiag [~5m]
- Add a one-line mirror of the mimalloc-new-delete.h fork-maintenance note [~5m]

## Critical files

- `Engine/Source/Memory/MemoryManager.cpp`
- `Common/ExternalHeaders.h`
- `Engine/Source/Memory/CLAUDE.md`

## Out of scope

- The stale `Memory/MemoryManager.h` consumer includes, the `CollectionMemory.h:3` header channel, and
  `ProfileManagerBase.cpp`'s missing direct include — appended to `Engine/DeadCodeAndUnusedIncludesSweep.md`
  (this run).
- Adding `MemoryManager.h` to the `Engine.h` aggregation — its real consumer set is two TUs (`Main.cpp`,
  `ProfileManagerBase.cpp`); direct includes are the right shape once the sweep lands.
- Static-init-order hardening for `gMemoryInitializer` (`:135`) — observation only; no allocating earlier
  initializer was found, and the shutdown over-reserve `DEBUG_BREAK` (`:127-130`) already surfaces the symptom.
- Wrapping the globals in `engine::` — the "Manager-that-isn't" naming/doc tension is owned by
  `Graphics/ParentRuleFramingReconciliation.md` Decision 3.

## Notes

- No determinism/CRC exposure — allocation tracking is diagnostics-only; the allocator backend is untouched.
- The atomic change touches the global-allocator hot path but is mechanical and compile-checked; no grill
  decisions pre-staged.

## Verification Notes

- Verification pass (this run) confirmed every citation against source: `sbTrackingReady` plain `bool`
  (`MemoryManager.cpp:10`), cross-thread read `:19`, main-thread store `:32`; `giAllocationsThisFrame`'s
  relaxed discipline `:16`; the mi_new operator block `:61-81` is indeed a hand-maintained fork of
  `ThirdParty/mimalloc/include/mimalloc-new-delete.h` (header exists; stock version is bare `mi_new*` calls
  with no hook point); `EnableAllocationTracking` calls at `Main.cpp:256/:317`; tracking-eligible threads live
  at the `:256` store all confirmed (`Multithreading` workers `Main.cpp:85`, TextureUploadManager thread
  `TextureUploadManager.cpp:151`, FileManager eager/lazy load threads `FileManager.cpp:318/:463`, DxDiag
  `std::async` task constructing a `ThreadLocal` at `CrashReport.cpp:81`); `ExternalHeaders.h` std block spans
  `:59-102` and contains neither `<new>` nor `<cstdlib>`; `Memory/CLAUDE.md:13` "bare `std::async` tasks"
  wording confirmed.
- Precision note for the `<cstdlib>` item: it covers the `malloc`/`free` calls in the `ENABLE_CRT_DEBUG_HEAP`
  block (`:37-57`); the `_aligned_malloc`/`_aligned_free` calls there are UCRT extensions (not `<cstdlib>`),
  already reached via `windows.h`. `<new>` is the load-bearing addition for the always-compiled block
  (`std::align_val_t`/`std::nothrow_t`).
- No overlap with live plans (`Common/ThreadSafety_SharedStaticBuffers.md` covers different statics; the
  Manager-naming tension is correctly deferred to `Graphics/ParentRuleFramingReconciliation.md` Decision 3;
  the consumer-include cleanup is correctly routed to `Engine/DeadCodeAndUnusedIncludesSweep.md`).
- No corrections required; no items dropped.
