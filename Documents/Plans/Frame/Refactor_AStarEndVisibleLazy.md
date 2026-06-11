# Refactor: Lazy A* End-Visibility Memoization

## Context

Source: /external-refactor-clean on `Engine/Source/Frame` (non-recursive). `AStarPath` (`NavQuery.cpp:366-577`) eagerly precomputes start- and end-visibility for **all** V cell vertices per invocation (lines 383–387, two O(V) LOS sweeps over the DDA-accelerated edge grid). The start side is optimal as-is — the start node expands to all V vertices on its first pop (lines 542–548). The end side is not: `pEndVisible` is consumed only per *expanded* vertex (line 564), so the eager O(V) sweep is wasted whenever A* terminates after a small frontier — the typical case. Tick-time path: `NavQueryDirection` → per-player navigation (`PlayersNavigation.cpp:280/336/394`), already mitigated by the direct-LOS fast path and caller throttling, but V plausibly reaches high hundreds per cell (`kiMaxIslandsPerCell = 107`).

## Design

### Engine/Source/Frame/NavQuery.cpp
- Convert `pEndVisible` to a lazily-memoized tri-state (`int8_t` unknown/visible/blocked) in the same `AStarMemory` workbuffer block (same byte budget after rounding): compute each entry on first query at the line-564 consumption site instead of eagerly at lines 383–387. Keep `pStartVisible` eager (fully consumed by design) [~30m]
- Update `ComputeAStarMemorySize`/`PartitionAStarMemory` for the `int8_t` array if the type changes [~10m]

## Critical files
- `Engine/Source/Frame/NavQuery.cpp`

## Acceptance criteria
- Identical paths returned for identical inputs (visibility is a pure function of static geometry — memoization cannot change results, only when they're computed).
- Worst-case per-invocation LOS precompute roughly halves; no allocation added (stays in the workbuffer arena).

## Out of scope
- Restructuring `AStarPath` itself (owned by `Refactor_NavFunctionDecomposition.md` — co-schedule; both edit the same function)
- Changing LOS/visibility semantics, epsilons, or the heap tie-break
- Caller-side throttling changes (`PlayersNavigation.cpp`)

## Notes
- Determinism exposure: none — results are bit-identical regardless of evaluation order (pure static-geometry queries; NavData is outside the CRC). Still a sim-path edit; verify with a replay after landing.
- Shares `AStarPath` with `Refactor_NavFunctionDecomposition.md` — land in one session to avoid stale line refs.

## Verification Notes (2026-06-10)
- Laziness soundness confirmed against source: the eager dual sweep is `NavQuery.cpp:383-387`; `pStartVisible` is fully consumed (all V entries) when the start node pops first (`:540-548` — start is the only initial heap entry, so this is the first pop) and only *re-read* per expanded vertex afterwards (`:569-571`, no recompute) — keeping it eager is correct. `pEndVisible` is read solely at `:564`, once per *expanded* vertex — lazy memoization computes exactly the entries A* touches and `SegmentBlockedByObstacle` is a pure function of static NavData (its own header comment at `:36-41` documents order-independent determinism), so results are bit-identical.
- Byte-budget claim holds: `bool` → `int8_t` tri-state is 1 byte either way; `ComputeAStarMemorySize` (`:327-340`) and `PartitionAStarMemory` (`:342-362`) place the two visibility arrays last, so no alignment ripple.
- Context figures verified: `kiMaxIslandsPerCell` = 1+2+3 + 6×16 + 5 = 107 (`IslandChainPlacement.cpp:66-67`); A* runs only after the direct-LOS fast path fails (`:671-682`); tick-time callers at `PlayersNavigation.cpp:280/336/394` with caller-side throttling documented at `:255`. Note V is the cell's total contour *vertex* count (vertices per island × islands), so the O(V) sweep cost claim is, if anything, understated.
