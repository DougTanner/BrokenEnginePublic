# Workbuffer Alignment Guarantee + Frame-Accounting Integrity

## Summary

**What this plan does:** Adds a 16-byte alignment guarantee to the `common::Workbuffer` per-thread stack allocator (`Common/Workbuffer.{h,cpp}`) — `RawPush`/`RawPushBuffer` round the new frame base up to 16 so every reservation starts SIMD-aligned — plus five frame-accounting integrity riders in the same two files: a Release-observable `Grow` under-size log, a `Pop` top-of-stack identity ASSERT, `ShrinkLastPushBuffer` staleness reset + bound check, depth-0 write ASSERTs across the `Append`/`AppendFloat`/`PushBack` family, and a grow-before-mutate reorder in `RawPushBuffer`. Updates the `Common/CLAUDE.md` Workbuffer bullet to state the new 16-byte frame-start guarantee (and makes its existing "read/append at depth 0 asserts" line true).

**Why it's good for the codebase:** Closes a verified access-violation crash class. The odd-spaceship-count render path opens a workbuffer frame of `rCurrent.iCount * sizeof(int64_t)` (`SpaceshipsRender.cpp:96`) whose end lands at 8-mod-16; `AnimationData` then nests `PushBuffer<XMMATRIX*>` / `reinterpret_cast<XMVECTOR*>` reservations through it and does `movaps` stores (`AnimationData.cpp:187`, `:349-364`) → misaligned SIMD store → AV. Rounding the frame base up to 16 makes **every** `PushBuffer<T>` reservation SIMD-safe by construction (not just the two known sites). The five riders convert today's silent Release heap corruption and accounting drift (`Grow` dangling outer pointers, non-LIFO `Pop`, stale shrink guard, depth-0 leak, phantom frame on `bad_alloc`) into debug ASSERTs and post-mortem-diagnosable logs.

## Context

- Source: `Documents/Plans/Common/WorkbufferAlignmentAndFrameIntegrity.md` (claimed; removed with its Order.md row after execution completes)
- Order.md row: Tier Small / Effort 2 / Impact 4 / Risks 1 / Score -1 (marked `[CLAIMED]` in Step 2)
- Notes: Fix the workbuffer's missing alignment guarantee — `RawPush`/`RawPushBuffer` never align `miBase`, and the live odd-spaceship-count render path (`SpaceshipsRender.cpp:96` frame → nested `AnimationData` `XMVECTOR`/`XMMATRIX` stores) does `movaps` through 8-mod-16 pointers (verified crash class). Round frame bases up to 16; riders: Release `Grow` kError log, `Pop` frame-identity ASSERT, stale `ShrinkLastPushBuffer` depth reset, depth-0 append ASSERTs (docs claim they exist), grow-before-push exception balance. No CRC exposure (scratch addresses only). Subsumes `Frame/Refactor_FrameRootQuickWins.md`'s Span alignment-ASSERT item.
- Relevance: **Fully relevant** — every edit site (`RawPush`, `RawPushBuffer`, `Pop`, `ShrinkLastPushBuffer`, `Append`×3, `AppendFloat`, `PushBack`, `Grow`) and every crash-premise citation resolves against current code; line numbers match with no drift.
- Dependency resolution: none (top three table rows were `[CLAIMED]` by other sessions; this was the first unclaimed row and has no unmet prerequisites — the only Dependencies bullet naming it, the "Workbuffer pair", has *this* plan as the prerequisite of `Frame/Refactor_FrameRootQuickWins.md`, not the reverse).
- Changes since the plan was written: none. All six design items and both crash-premise citations verified against current source this session (`Common/Workbuffer.h` / `.cpp`, `SpaceshipsRender.cpp:96`, `AnimationData.cpp:187,349-364`). `AnimationData.cpp` lives at `Engine/Source/Graphics/AnimationData.cpp` (plan cites it path-relative, so no citation edit needed).
- **2026-07-03 origin.** Found in a `Common/` review sweep; verified end-to-end (allocator code + both call sites read that session, re-verified this session).

## Execution steps

Current citations are `path:line` against verified source; ranges are the full function bodies.

1. **Alignment (the fix)** — in `RawPush` (`Common/Workbuffer.h:82`) and `RawPushBuffer` (`Common/Workbuffer.h:98`), after saving the previous base into `mSavedBase[miDepth]`, round the new base up to 16 with `common::RoundUp` (`Common/Math/MathUtils.h:123`, the 2-arg `RoundUp(iToRound, iMultiple)`) instead of the bare `miBase = miSize`. In `RawPushBuffer` this makes every reservation start 16-aligned. ≤15 bytes overhead per frame. 16 covers `XMVECTOR`/`XMMATRIX` (the only over-aligned types used); do NOT add a per-call alignment parameter (YAGNI). `Pop` (`Workbuffer.h:124`) already restores the exact saved value, so no unwind change is needed. Add a one-line comment stating the 16-byte frame-start guarantee at the class doc block (near `Workbuffer.h:34-35`, the `Append`/`View`/`Span` frame-relative comment).
   - Note the interaction with step 6: the reordered `RawPushBuffer` must compute `iNeeded` from the *aligned* new base (`RoundUp(miSize, 16) + iSizeInBytes`) so the `Grow` check and the post-assignment `miSize` stay consistent.
2. **Grow observability** — in `Grow` (`Common/Workbuffer.cpp:72`), when `miDepth > 0`, emit `LOG(kDefault, kError, ...)` before the resize so Release under-sizing is diagnosable post-mortem. The allocation-suppressed scope (`ScopedSuppressAllocationTracking suppress;` at `Workbuffer.cpp:79`) is already present; keep the resize (graceful degradation stays). `Grow` is a member and can read `miDepth` directly.
3. **Pop identity** — store the owning depth in `ScopedWorkbufferArena` (`Workbuffer.h:146`) / `ScopedWorkbufferAllocation` (`Workbuffer.h:176`) at construction; add a top-of-stack ASSERT (either `Pop(int64_t iExpectedDepth)` or a member ASSERT before the `mBuffer.Pop()` call at `Workbuffer.h:156` / `:185`) asserting `miDepth` equals the handle's stored depth on destruction. Moved-from handles (`mpBuffer == nullptr`) stay inert and skip the check.
4. **Shrink staleness** — reset `miLastPushBufferDepth = -1` in `Pop` (`Workbuffer.h:124`) and `RawPush` (`Workbuffer.h:82`); add `ASSERT(iActualSize >= 0 && iActualSize <= miLastPushBufferSize)` in `ShrinkLastPushBuffer` (`Workbuffer.h:57`), alongside the existing `ASSERT(miDepth == miLastPushBufferDepth)`.
5. **Depth-0 write asserts** — add `ASSERT(miDepth > 0)` to the four write overloads in `Common/Workbuffer.cpp`: `Append(std::string_view)` (`:6`), `Append(std::wstring_view)` (`:18`), `Append(int64_t)` (`:34`), `AppendFloat(float, int)` (`:50`); and to `PushBack<T>` (`Common/Workbuffer.h:43`). This matches the documented contract at `Workbuffer.h:34-35` and `Common/CLAUDE.md` ("read/append at depth 0 asserts"), which `View`/`Span` already satisfy but the write family does not. The `LOG` formatter path always runs inside an open per-argument frame, so no exemption is needed; verify with a client run of the log-heavy boot path.
6. **Grow-before-push** — in `RawPushBuffer` (`Workbuffer.h:98`), compute `iNeeded` and call `Grow` **before** mutating `mSavedBase`/`miDepth`/`miBase`, so a `bad_alloc` mid-grow unwinds with balanced accounting instead of leaving a phantom open frame forever. (The `mSavedBase` capacity-grow block at `Workbuffer.h:101-108` already runs before it mutates `mSavedBase[miDepth]`, so it is already balanced; only the `Grow(iNeeded)` call at `:113-116` needs to move earlier.)
7. **Docs** — update the `Common/CLAUDE.md` Workbuffer bullet: state the 16-byte frame-start guarantee; the "read/append at depth 0 asserts" line becomes fully true after step 5.

## Critical files

- `Common/Workbuffer.h` — `RawPush`, `RawPushBuffer`, `Pop`, `ShrinkLastPushBuffer`, `PushBack`, `ScopedWorkbufferArena`, `ScopedWorkbufferAllocation`, class doc block
- `Common/Workbuffer.cpp` — the three `Append` overloads + `AppendFloat`, `Grow`
- `Common/CLAUDE.md` — Workbuffer bullet: 16-byte frame-start guarantee; the depth-0 assert line becomes true

## Invariant exposure

- **No CRC/determinism impact**: the workbuffer is per-thread scratch; alignment padding changes addresses, never values, and no workbuffer bytes enter serialization or CRC. Sim code using the workbuffer (collision results, `ActiveFrameRef`) computes identical values at shifted addresses.
- Allocation-tracked paths: all edits are inside already-suppressed or non-allocating code; the new `LOG` in `Grow` sits next to the existing suppress scope (`Workbuffer.cpp:79`).
- No `kiVersion` / `.pack` / wire / replay / client-server-guard exposure.

## Affected (non-edited) call sites — for `/update-affected-code`

- `Projects/BrokenEngineSandbox/Source/Ui/Screens/MenuUtils.cpp:44` — the only external `ShrinkLastPushBuffer` caller (`rWorkbuffer.ShrinkLastPushBuffer(pcWrite - pcBase)` in `AppendUtf8`). **No edit needed**, but it is the one site whose behavior items 3/4 govern; the reviewer should re-confirm after the edit lands that (a) the shrink still runs at the same depth as its `PushBuffer<char*>` with no intervening `Push`/`Pop` (so the reset-on-`Pop` change keeps `miDepth == miLastPushBufferDepth` intact) and (b) `iActualSize = pcWrite - pcBase <= miLastPushBufferSize` so the new bound ASSERT holds. Verified compatible this session.

## Out of scope

- The `Workbuffer::Span<T>` alignment ASSERT item in `Frame/Refactor_FrameRootQuickWins.md` — **subsumed by step 1** (a structural guarantee beats an assert); when this plan lands first, drop that item there (or land them together).
- Per-call configurable alignment / over-64 alignment support.
- Any redesign of the move-only allocation handle (`Adopt` has zero callers — leave it).
- `Multithreading.h` / `PersistentWorker` changes (separate plan: `Common/CommonPrimitivesHardening.md`).
- Auditing every existing `PushBuffer` call site for other alignment assumptions — step 1 makes them all safe by construction. (Sweep confirmed no other custom bump/stack allocator shares this class; `CollectionMemory.h` is 64-aligned, the `PreCollision` `thread_local std::vector` scratch is `operator new`-aligned.)

## Acceptance criteria

- `RawPushBuffer` returns 16-aligned pointers regardless of prior frame sizes (odd-count `SpaceshipsRender` path included).
- Client + server build; a client run with animated spaceships (odd counts occur naturally) shows no regression.
- Non-LIFO handle destruction and depth-0 appends now ASSERT in debug.

## Additional candidate locations

No additional candidates found. A dedicated Opus sibling-pattern sweep this session checked, in both directions (oversights + drift), for (A) other custom scratch/bump allocators handing out raw bytes cast to 16-byte SIMD types without alignment, (B) other frame-relative `Append`/`PushBack`/`View`/`Span`-style accessors missing a depth guard, and (C) external readers of `miLastPushBuffer*` / `ShrinkLastPushBuffer` affected by items 3/4:

- (A) — none. `common::Workbuffer` is the only custom CPU bump/stack allocator with this latent movaps-fault class. `CollectionMemory.h` (`AssignAligned` → 64-byte-aligned base via `AlignedMemory.h` `MakeAligned`) and the `PreCollision`/combat `thread_local std::vector` scratch buffers (typed, `operator new`-aligned, no raw-byte→SIMD cast) verified SAFE.
- (B) — none beyond step 5's own targets (`Append`×3, `AppendFloat`, `PushBack`). No sibling scratch type exposes frame-relative accessors; `ScopedWorkbufferArena`'s forwarders delegate to the guarded `mBuffer.*` methods and inherit the assert.
- (C) — one downstream site, `MenuUtils.cpp:44`, recorded above under "Affected call sites"; confirmed compatible, no edit.

## Notes

- Grill decision pre-staged: none — all items are mechanical with one obvious shape. The only judgment call (align-up vs alignment parameter) is resolved above (align-up, KISS).
