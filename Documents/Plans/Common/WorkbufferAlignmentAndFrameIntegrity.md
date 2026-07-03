# Workbuffer Alignment Guarantee + Frame-Accounting Integrity

## Context

2026-07-03 review sweep of `Common/`. The workbuffer stack allocator (`Common/Workbuffer.h`/`.cpp`) provides **no alignment guarantee**: `RawPush`/`RawPushBuffer` set `miBase = miSize` at whatever byte offset the enclosing frame left. One live path stores 16-byte-aligned SIMD types through it:

- `SpaceshipsRender.cpp:96` (`SpaceshipsPostRender::Render` pass 1) opens a frame of `rCurrent.iCount * sizeof(int64_t)` bytes — **odd ship count → frame end is 8-mod-16**.
- The main thread then runs the `Dispatch` remainder range (`Multithreading.h` main-thread chunk) with that frame still open; `AnimationData::EvaluateAnimation`/`EvaluateWorldMatrices` (`AnimationData.cpp:187, 349-364`) nest a `PushBuffer<std::byte*>`, `reinterpret_cast` it to `XMVECTOR*`/`XMMATRIX*`, and assign — `movaps` stores through a misaligned pointer → access violation. Worker threads escape only because their workbuffers are at depth 0 when the lambda starts (`std::vector` backing data is 16-aligned via default `operator new`).

Verified end-to-end (allocator code + both call sites read this session). Secondary integrity gaps found in the same review, all in the same file:

1. **Release `Grow` silently invalidates outstanding pointers** (`Workbuffer.cpp` `Grow`): `DEBUG_BREAK()` fires only under a debugger; the resize proceeds, dangling every live `ScopedWorkbufferAllocation::mpData`/`View`/`Span` from outer frames — silent heap corruption, not the "gameplay never fails" the comment claims.
2. **`Pop` has no frame-identity check** (`Workbuffer.h` `Pop`): the move-only `ScopedWorkbufferAllocation` supports non-scope-local lifetimes, but non-LIFO destruction of two live handles silently corrupts `miBase`/`miSize` accounting. No violating call site exists today (all uses scope-local; `Adopt` has zero callers) — latent.
3. **`ShrinkLastPushBuffer` guard is fooled by depth reuse**: `Pop` never invalidates `miLastPushBufferDepth`/`miLastPushBufferSize`, so a later frame at the same numeric depth passes the `ASSERT(miDepth == miLastPushBufferDepth)` against a stale size. Also no `iActualSize <= miLastPushBufferSize` bound.
4. **`Append`/`PushBack` at depth 0 not asserted**, contradicting `Workbuffer.h:34-35` and `Common/CLAUDE.md` ("read/append at depth 0 asserts") — a depth-0 append permanently leaks `miSize` for the thread.
5. **`RawPushBuffer` increments depth before a throwing `Grow`**: `bad_alloc` mid-grow leaves a phantom open frame forever.

## Design

1. **Alignment (the fix)**: in `RawPush` and `RawPushBuffer`, round `miBase` up to 16 (`common::RoundUp`) after saving the previous base; in `RawPushBuffer` this makes every reservation start 16-aligned. ≤15 bytes overhead per frame. 16 covers `XMVECTOR`/`XMMATRIX` (the only over-aligned types used); do NOT add a per-call alignment parameter (YAGNI). `Pop` already restores exact saved values so no unwind change is needed. Add a one-line comment stating the 16-byte frame-start guarantee at the class doc block.
2. **Grow observability**: in `Grow`, when `miDepth > 0`, `LOG(kDefault, kError, ...)` before the resize (allocation-suppressed scope already present) so Release under-sizing is diagnosable post-mortem. Keep the resize (graceful degradation stays).
3. **Pop identity**: store the owning depth in `ScopedWorkbufferArena`/`ScopedWorkbufferAllocation` at construction; `Pop(int64_t iExpectedDepth)` (or member ASSERT before the call) asserts top-of-stack destruction. Moved-from handles stay inert.
4. **Shrink staleness**: reset `miLastPushBufferDepth = -1` in `Pop` and `RawPush`; add `ASSERT(iActualSize >= 0 && iActualSize <= miLastPushBufferSize)` in `ShrinkLastPushBuffer`.
5. **Depth-0 write asserts**: add `ASSERT(miDepth > 0)` to all four `Append` overloads and `PushBack` — makes the code match the documented contract (the `LOG` formatter path always runs inside an open per-argument frame, so no exemption needed; verify with a client run of the log-heavy boot path).
6. **Grow-before-push**: in `RawPushBuffer`, compute `iNeeded` and call `Grow` before mutating `mSavedBase`/`miDepth`/`miBase` so a `bad_alloc` unwinds with balanced accounting.

## Critical files

- `Common/Workbuffer.h` — `RawPush`, `RawPushBuffer`, `Pop`, `ShrinkLastPushBuffer`, `PushBack`, `ScopedWorkbufferArena`, `ScopedWorkbufferAllocation`
- `Common/Workbuffer.cpp` — `Append` overloads, `Grow`
- `Common/CLAUDE.md` — Workbuffer bullet: state the 16-byte frame-start guarantee; the "read/append at depth 0 asserts" line becomes true

## Invariant exposure

- **No CRC/determinism impact**: the workbuffer is per-thread scratch; alignment padding changes addresses, never values, and no workbuffer bytes enter serialization or CRC. Sim code using the workbuffer (collision results, `ActiveFrameRef`) computes identical values at shifted addresses.
- Allocation-tracked paths: all edits are inside already-suppressed or non-allocating code; the new `LOG` in `Grow` sits next to an existing suppress scope.

## Out of scope

- The `Workbuffer::Span<T>` alignment ASSERT item in `Frame/Refactor_FrameRootQuickWins.md` — **subsumed by item 1** (a structural guarantee beats an assert); when this plan lands first, drop that item there (or land them together).
- Per-call configurable alignment / over-64 alignment support.
- Any redesign of the move-only allocation handle (`Adopt` has zero callers — leave it).
- `Multithreading.h` / `PersistentWorker` changes (separate plan: `Common/CommonPrimitivesHardening.md`).
- Auditing every existing `PushBuffer` call site for other alignment assumptions — item 1 makes them all safe by construction.

## Acceptance criteria

- `RawPushBuffer` returns 16-aligned pointers regardless of prior frame sizes (odd-count `SpaceshipsRender` path included).
- Client + server build; a client run with animated spaceships (odd counts occur naturally) shows no regression.
- Non-LIFO handle destruction and depth-0 appends now ASSERT in debug.

## Notes

- Grill decision pre-staged: none — all items are mechanical with one obvious shape. The only judgment call (align-up vs alignment parameter) is resolved above (align-up, KISS).
