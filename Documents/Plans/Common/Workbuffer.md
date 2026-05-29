# Workbuffer: Frame-Relative Read Hazards and Hidden Allocations

## Context

`common::Workbuffer` (`Common/Workbuffer.h`, `Common/Workbuffer.cpp`) is the codebase-wide thread-local LIFO bump allocator reached via `gpThreadLocal->mWorkbuffer`. It backs the LOG `Wb`/`WbV2` float wrappers, the arena `std::formatter`, `EnumToString`-style scratch returns, and ad-hoc scratch frames throughout DataPacker, Engine, and game code. Because it is a pervasive scratch allocator, latent correctness traps in its read-back and growth paths affect many call sites.

This plan was distilled from a code-analysis report validated against the source. The report's symbols/lines all matched real source (not a hallucination). Findings were pruned to genuine correctness defects; pure input-validation asks, speculative API renames, thread-affinity doc asks, and cosmetic style notes were dropped.

Design constraint preserved: the `Grow()` `DEBUG_BREAK()` is BY DESIGN — buffers must be sized up front (see `Common/CLAUDE.md` Workbuffer section). This plan does NOT propose removing that invariant. It targets the *pointer-invalidation* side effect of the growth path and the *frame-relative read* coupling.

## Design

### 1. `View()` / `Span<T>()` base-offset coupling to `miBase` (HIGH) — Effort 2
- `View()` returns `[mBuffer.data() + miBase, miSize - miBase)` (`Common/Workbuffer.cpp:50-53`); `Span<T>()` spans `[miBase, miSize)` (`Common/Workbuffer.h:55-65`).
- `miBase` is the start of the *current top frame*: `RawPush`/`RawPushBuffer` set `miBase = miSize` (`Common/Workbuffer.h:77`, `:89`); only `Pop` restores it (`:106`). Raw `Append` advances `miSize` only — it never updates `miBase`.
- Two real failure modes:
  - **Bare `Append` then `Push()` before reading**: the `Push()` jumps `miBase` up to `miSize`, so the subsequent `View()`/`Span()` returns an empty range and the appended text is invisible to the reader.
  - **Bare `Append` with no surrounding `Push()`**: `miBase == 0`, so `View()` returns `[0, miSize)` — i.e. the appended text plus any leading bytes still logically live in the buffer from earlier in the tick.
- The documented arena idiom (`arena = Push(); arena.Append(...); LOG("{}", arena)`) is safe because the formatter reads `View()` inside the same open frame. The hazard is that the same `Append`/`View`/`Span` surface is exposed *bare* on `Workbuffer` and the frame-relative read contract is implicit.
- **Fix (design-consistent, debug-only)**: make the read-back contract explicit — `ASSERT(miDepth > 0)` in `View()` and both `Span<T>()` overloads so any bare read outside an open frame fails fast in debug, and document on `Workbuffer` that `Append`/`View`/`Span` are valid only within an open frame. Prefer this over re-tracking an append-run start offset (smaller surface, matches the arena-centric design).

### 2. `Grow()` reallocates the ThreadLocal-owned backing vector, invalidating outstanding handles (HIGH) — Effort 1
- `Grow()` (`Common/Workbuffer.cpp:55-59`) does `DEBUG_BREAK(); mBuffer.resize(iNeededCapacity * 2);`. `mBuffer` is a *reference* to externally owned storage (`std::vector<std::byte>& mBuffer;`, `Common/Workbuffer.h:111`; the backing vector is owned by `ThreadLocal` and passed by reference to the ctor, `:14-18`).
- The `DEBUG_BREAK()` (sized-up-front invariant) is by design and stays. The genuine hazard is the `mBuffer.resize(...)` that follows: if it ever executes (release/no-debugger build, where `DEBUG_BREAK()` does not fire), it reallocates the shared backing vector, silently invalidating every outstanding `View()` (`std::string_view` into old storage), `Span<T>()`, and `ScopedWorkbufferAllocation<T>` typed pointer obtained before the grow. The same reachability exists from the `Append(int64_t)`/`AppendFloat` recovery paths (`Common/Workbuffer.cpp:25`, `:41`), which call `Grow` and then correctly re-derive their own `pStart`/`pEnd` from the new base — but any *caller-held* pointer is left dangling.
- **Fix (consistent with the up-front-sizing design)**: drop the `mBuffer.resize(...)` from `Grow()` so the function is a pure fatal guard (`DEBUG_BREAK()` then no silent reallocation of shared storage), and add a comment that growth is unsupported because the buffer is sized up front and a reallocation would invalidate all outstanding `View`/`Span`/`PushBuffer` handles. Do NOT add error handling beyond the existing break. The `Append(int64_t)`/`AppendFloat` retry-after-`Grow` blocks become dead-by-design but are harmless; leave them or simplify only if trivial.

### 3. `mSavedBase` growth is a hidden main-loop heap allocation (MEDIUM) — Effort 2
- `RawPush` and `RawPushBuffer` do `mSavedBase.resize(mSavedBase.size() * 2)` when nesting depth reaches capacity (`Common/Workbuffer.h:71-74`, `:83-86`). `mSavedBase` is the Workbuffer's own member `std::vector<int64_t>`, ctor-sized to 8 (`:17`, `:116`).
- This `resize` is a real heap allocation that would trip the main-loop allocation tracker `DEBUG_BREAK()` (see root `CLAUDE.md` allocation-tracking directive / `Engine/Source/Memory/CLAUDE.md`). Eight levels of arena nesting is plausible for loop/lambda-driven content builders, so this is reachable in steady state rather than purely defensive.
- **Fix**: pre-size `mSavedBase` once in the ctor to a depth that source structure provably never exceeds, and replace the runtime `resize` with `ASSERT(miDepth < static_cast<int64_t>(mSavedBase.size()))` in both `RawPush` and `RawPushBuffer`. (Alternative if a hard bound is undesirable: wrap the rare resize in `ScopedSuppressAllocationTracking` with a `// Heap:` comment — but the assert is preferred since depth is bounded by call structure.)

### 4. `ShrinkLastPushBuffer` corrupts size accounting when a `Push` intervened (MEDIUM) — Effort 1
- `ShrinkLastPushBuffer` (`Common/Workbuffer.h:49-53`) does `miSize -= (miLastPushBufferSize - iActualSize)` using `miLastPushBufferSize`, which is set *only* in `RawPushBuffer` (`:96`) and never in `RawPush`. The comment at `:47-48` acknowledges "Push frames don't update the tracked size."
- If a bare `Push` frame opens after a `PushBuffer` and `ShrinkLastPushBuffer` is then called, it adjusts `miSize` (the current top frame's end) using the stale size from the now-deeper `PushBuffer` frame, corrupting the live frame's size accounting. The only guard today is the comment.
- **Fix (debug-only, design-consistent)**: record the depth at which `miLastPushBufferSize` was set (e.g. a `miLastPushBufferDepth` member set in `RawPushBuffer`) and `ASSERT(miDepth == miLastPushBufferDepth + 1)` (or equivalent top-frame check) at the start of `ShrinkLastPushBuffer` so the misuse fails fast.

## Critical files
- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\Workbuffer.h` — `View`/`Span` asserts, `RawPush`/`RawPushBuffer` `mSavedBase` pre-size + assert, `ShrinkLastPushBuffer` depth guard, ctor pre-size.
- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\Workbuffer.cpp` — `Grow()` drop the shared-buffer `resize`; `View()` assert; numeric-append retry blocks become dead-by-design.

## Out of scope
- Removing or weakening the `Grow()` `DEBUG_BREAK()` sized-up-front invariant — it is by design (`Common/CLAUDE.md`). Only the silent `mBuffer.resize` side effect is in scope.
- Input-validation / overflow guards on `iSizeInBytes`, `text.size()`, `iActualSize`, or signed size math (`miSize + sizeof(T)`, `miBase + iSizeInBytes`) — the project assumes valid parameters (root `CLAUDE.md`: "DO NOT add error handling or validation").
- `Span<T>()` alignment / `sizeof(T)`-divisibility checks — guarded by the fill-with-`T` convention; not a code defect.
- Cross-thread affinity documentation / owning-thread-id asserts — per-thread by construction via `ThreadLocal`; no call site misuses it.
- Renaming the `ScopedWorkbufferAllocation` pointer template parameter, `std::memcpy` vs `memcpy`, and trailing-comment whitespace — cosmetic / speculative; let `.editorconfig` handle spacing.
- Any change to `LogFormatters.h`, `Wb`/`WbV2`, or call sites — the formatter idiom is correct as written.

## Acceptance criteria
- `View()` and both `Span<T>()` overloads assert `miDepth > 0`; bare reads outside an open frame fail fast in debug.
- `Grow()` no longer reallocates the shared `mBuffer`; it is a pure fatal guard, with a comment explaining why growth is unsupported and what it would invalidate.
- `mSavedBase` no longer reallocates at runtime: ctor pre-sizes it and `RawPush`/`RawPushBuffer` assert depth is within capacity. No path through `Push`/`PushBuffer` performs a heap allocation in steady state.
- `ShrinkLastPushBuffer` asserts the tracked `PushBuffer` size belongs to the current top frame.
- Existing arena/LOG idioms (`Push()` → `Append` → `View()`/`{}`), `PushBuffer<T>`, and `EnumToString::Convert`-style `Adopt` returns continue to behave unchanged; all affected projects compile.

## Notes
- Findings 2 and the `Append(int64_t)`/`AppendFloat` "re-derive after Grow" concern from the report share one root cause (`Grow` reallocating shared storage) and are addressed together by Design item 2.
- Report findings dropped during validation: unchecked signed size math (input-validation), `Span<T>` alignment/divisibility (convention-guarded validation), single-thread affinity doc/assert (no real defect), `memcpy`→`std::memcpy` and naming/whitespace (cosmetic), pointer-type template-param rename (speculative API). No findings were dropped as phantom — all cited symbols/lines exist in source.
- All fixes are debug-only asserts plus removal of one silent `resize` and one ctor pre-size; no behavioral change on the happy path, keeping risk to this widely-used allocator low.
