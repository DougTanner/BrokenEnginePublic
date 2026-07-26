<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-26T18:56:26.343Z","dependsOn":[]} -->
# NavQuery Per-Tick Pathfinding Cost Exceeds Its Budget

## Context

The whole-cell world-space visibility graph change (landed 2026-07-26) fixed the island-chain pathfinding
livelock and, as its measured cost, left `kCpuTimerPostRenderUpdateNavQuery` far above the ceiling its own
acceptance criterion set. That criterion — cohort mean `averageUs` at most **21.31** (1.25x a recorded
pre-change baseline of 17.05) and `maxUs` at most **1062.5** — did not pass and was routed here.

**Measured post-change**, three runs on the same tree, differing only in the nav broad-phase grid
`kiNavZonesX`/`kiNavZonesY` (`Engine/Source/Frame/NavBuild.h:19-20`):

| `kiNavZonesX`/`Y` | cohort mean `averageUs` | cohort `maxUs` |
| --- | --- | --- |
| 16 | 113.70 | 477 |
| 32 | 94.95 | 525 |
| 64 | 93.20 | 430 |

The `maxUs` bound **passes** (430 <= 1062.5). The mean is **4.4x** over its ceiling.

Method: `query_profile` row `NavQuery`, sampled roughly every 25 ticks, two n=20 cohorts inside one process
lifetime with the first (warm-up) cohort discarded, 8 units actively pathing the south-west island chain in
cell `(0,0)`. Provenance: `Temp/run1-profiles.json`, `Temp/run2-profiles.json`, `Temp/run3-profiles.json`,
analyzed by `Temp/nav2-cohorts.ps1`. `Temp/` is gitignored and those artifacts will not survive, so the table
above is the record.

**The one pre-authorized lever is saturated.** Raising the broad-phase grid 16 -> 32 bought 16%; 32 -> 64
bought a further 1.8%. `kiNavZonesX`/`kiNavZonesY` now sit at 64. Another doubling cannot close a 4.4x gap,
and the acceleration structure is no longer where the time goes.

**Comparability caveat — the 17.05 baseline was not doing the work, and this plan must not chase it.**
That baseline was recorded on a pre-change run in which 3 of 8 units were livelocked *inside* nav polygons.
Such a unit takes the start-inside-obstacle branch in `NavQueryDirection`
(`Engine/Source/Frame/NavQuery.cpp`) and returns an escape bearing **before** `AStarPath` runs, so it performed
almost no pathfinding work. Post-change all 8 units run real A\* around an island chain. The two cohorts
therefore do not measure equal amounts of work, and the 1.25x ceiling was derived from a baseline that was not
doing the work being measured.

This does **not** excuse the regression: ~93 us mean with 8 units in one cell is a real per-tick cost on the
simulation thread and it scales with unit count. It means the ratio is not the right instrument. Establishing a
like-for-like baseline is the first required step of this plan, not an optional refinement.

**Candidate directions, recorded so the investigation does not start cold. None is chosen here; each needs
measurement before it is adopted.**

- *Trigger frequency.* The off-cadence recompute
  (`Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersNavigation.cpp:278-285`) forces
  `bRecompute` whenever `NavQueryPointBlocked` is true at the lookahead point (`kfNavLookahead = 8.0f`, `:32`)
  **or** at the current position. Inside the inflated nav polygon of an island chain that condition holds
  continuously, so the throttle at `kiNavRecomputeInterval = 16` (`:27`, applied at `:262-264`) is bypassed and
  pathfinding approaches once per tick per nearby unit. The comment at `:266-277` already states this
  ("Near obstacles this collapses to per-tick pathfinding", `:31`) as an accepted consequence.
- *Per-query work.* `AStarPath` (`NavQuery.cpp:412`) eagerly computes start visibility for **every** vertex
  (`:431-434`), calling `SegmentBlockedByObstacle` V times before A\* expands anything. End visibility is
  already lazy for exactly this reason (`:428-430`). Cell `(0,0)` measured 775 vertices.
- *Cadence.* `kiNavRecomputeInterval = 16` was deliberately left unchanged by the landed change and is
  untested as a lever.

## Design

This plan is decision-complete about the **problem**, deliberately not about the solution. Work it in this
order; do not skip to an optimization.

### 1. Establish a like-for-like baseline (required first)

Produce a cost figure both cohorts can be compared against honestly — one where the same amount of real
pathfinding is performed in both. Options include measuring only units that actually reach `AStarPath`,
measuring a scenario in which no unit is livelocked pre-change, or normalizing by A\* query count rather than
wall time per tick. Whatever is chosen, record the sample definition in this Plan before optimizing, and state
the resulting budget as an absolute per-tick figure at a stated unit count, not as a ratio against 17.05.

### 2. Attribute the cost before changing anything

Split the measured time into query **count** (how often `NavQueryDirection` runs) and query **cost** (work per
call). The three candidate directions above sit on opposite sides of that split and the wrong one cannot be
chosen without it. `kCpuTimerPostRenderUpdateNavQuery` wraps only the three `NavQueryDirection` call sites
(`PlayersNavigation.cpp:316`, `:372`, `:430`), so it already isolates query time from the rest of
`ComputeNavigation`; a query counter is the missing half.

### 3. Choose and validate one direction

Prefer, in order of preference and not of certainty: work removal at bit-identical output (no version bump, no
behavioral risk), then cadence or trigger change (moves CRC'd positions, needs a version bump and a fresh
livelock check), then algorithmic change to A\*.

**Any direction that changes which bearing a unit computes moves CRC'd unit positions and requires its own
`engine::kiNavDataVersion` bump** (`NavBuild.h:15`, currently 13), invalidating existing saves and replays. A
direction that only removes redundant work at identical output requires no bump — prove identity rather than
assuming it.

**Non-negotiable constraint: the livelock fix must survive.** Any trigger or cadence relaxation risks
reintroducing the throttle-overshoot the landed change fixed. The A\*-miss `kWarning`
(`NavQuery.cpp:703`) and the displacement scenario below are the guards.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersNavigation.cpp` — the sole
  `NavQueryDirection` consumer: throttle (`:262-264`), off-cadence containment trigger (`:266-285`), and the
  three profiled call sites (`:316`, `:372`, `:430`).
- `Engine/Source/Frame/NavQuery.cpp` — `AStarPath` (`:412`) and its eager start-visibility loop (`:431-434`);
  `SegmentBlockedByObstacle` (`:20`), the dominant inner predicate.
- `Engine/Source/Frame/NavBuild.h` — `kiNavDataVersion` (`:15`), `kiNavZonesX`/`kiNavZonesY` (`:19-20`).
- `Projects/BrokenEngineSandbox/Source/Profile/ProfileManager.h` — `kCpuTimerPostRenderUpdateNavQuery`
  (`:31`), the measurement instrument.

## Out of scope

- Re-raising `kiNavZonesX`/`kiNavZonesY`. Measured saturated (32 -> 64 bought 1.8%); do not re-run it as a
  lever.
- Reverting or weakening the whole-cell world-space visibility graph, or restoring per-template visibility.
  The livelock fix stands.
- `kfInflateDelta` being UV-relative — owned by `Documents/Plans/Frame/NavInflateMarginUvRelative.md`. It
  changes obstacle geometry and therefore vertex counts and containment area, which would confound every
  measurement here.
- Nav **build** cost (`BuildCellNavData`, `BuildNavAcceleration`). One-shot per cell, already inside its
  landed budget, and not what this timer measures.
- The start-inside-obstacle escape branch in `NavQueryDirection`, `NavMissFallbackDirection`, and
  `NavQuerySnapToNavigable` behavior.
- Spaceships navigation, which does not use NavQuery.
- Making pathfinding asynchronous, moving it off the simulation thread, or adding a path cache shared between
  units — an architectural change; if measurement points there, stop and raise it with the user rather than
  implementing it under this Plan.

## Risk tier and invariants

**Tier 3** unless the chosen direction is proven bit-identical in output, in which case it is Tier 2.
Triggers: determinism/CRC (pathfinding output feeds CRC'd unit positions) and save/replay compatibility
(`engine::kiNavDataVersion` is a summand of `game::Frame::kiVersion`). Classify at Tier 3 by default and
lower it only against a proven-identical-output demonstration.

Invariants to preserve: no RNG draw is added, removed, or reordered in `ComputeNavigation` — the recompute
throttle deliberately sits outside the draw sequence; client and server evaluate the recompute condition from
shared, wire-shipped state only, so the cadence stays identical on both sides; navigation data stays outside
the shared CRC and outside the persisted save payload; the A\* miss remains an invariant violation, not a
normal outcome.

## Acceptance criteria

1. A like-for-like baseline is recorded in this Plan — sample definition, unit count, tick window, and the
   absolute per-tick budget derived from it — before any optimization edit is made.
2. Cohort mean `averageUs` for `kCpuTimerPostRenderUpdateNavQuery` meets that recorded budget, measured with
   equal-sample cohorts in one process lifetime after a discarded warm-up cohort, in the same 8-unit
   south-west-chain scenario in cell `(0,0)` that produced 93.20.
3. Cohort `maxUs` does not regress above 430 in that same scenario.
4. Zero A\*-miss `kNavData` warnings (`NavQuery.cpp:703`) over at least 1000 ticks in that scenario — the
   livelock fix is intact.
5. Every unit in the three-phase island-chain repro either reaches a neighbour cell or shows at least 500
   units of net displacement over the 1000-tick window; no unit falls under 50 units.
6. At least 2000 ticks of live client plus server with zero `CONFIRMED DESYNC after full rollback/replay`, and
   replay determinism acceptance passes.
7. If the chosen direction changes computed bearings, `kiNavDataVersion` is bumped and a pre-change save is
   rejected with the expected version error rather than crashing. If it does not, a before/after comparison
   demonstrates identical unit positions over at least 500 ticks with no version bump.
8. `/compile` is clean for both the client and the server.

## Verification

Runtime through `/agent-harness`. Reuse the three-phase procedural repro that produced these measurements:
`reset`; `inject_status_changes` with eight `SpawnPlayer` entries at `coord:[0,0]` and
`fleetWantedCoord:[0,-1]`; after ~400 ticks `UpdateFleet` all eight to `fleetWantedCoord:[-1,0]`; after ~490
further ticks `UpdateFleet` all eight to `fleetWantedCoord:[0,1]`. Then sample `query_profile` for the
`NavQuery` row every ~25 ticks for two n=20 cohorts, discarding the first, and sample `query_players` across
`status.activeCoords` for the displacement table. Read A\*-miss lines with `get_logs {"category":"NavData"}`.

## Coordination

- `Documents/Plans/Frame/NavInflateMarginUvRelative.md` changes obstacle polygon geometry, which moves both
  the vertex count A\* iterates and the containment area that fires the off-cadence recompute. Whichever of the
  two Plans lands second must re-establish its baseline against the landed tree before claiming a measurement;
  do not carry a number across that landing. There is no required order and no co-land requirement.
