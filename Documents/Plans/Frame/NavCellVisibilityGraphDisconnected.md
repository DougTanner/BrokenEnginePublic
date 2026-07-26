<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-26T12:40:07.000Z","dependsOn":[]} -->
# Per-Cell Navigation Visibility Graph Is Disconnected Across Islands

## Context

Fleet ships in nav mode 0 (cardinal transit toward the fleet's wanted coord) livelock permanently against the
island chain in a cell, bouncing in and out of the inflated no-navigation polygon instead of routing around
it. Reproduced against a live server from a quicksave: **globalId 2 moved 4 units in 961 ticks**, and
globalId 4 showed the identical signature; the flagship (mode 4) and the mode-5 follower in the same fleet
only clipped the polygon for ~10 ticks and moved on.

**Root cause — a cell's A\* visibility graph is the disjoint union of per-island graphs.**
`BuildVisibilityGraph` (`Engine/Source/Frame/NavBuild.cpp:314`) runs per island **template** in UV space,
called from `BuildNavContour` (`NavBuild.cpp:553`, invoked at `Engine/Source/Frame/IslandTerrain.cpp:240`).
`BuildCellNavData` (`Engine/Source/Frame/NavCellData.cpp:255-262`) then only concatenates each placement's
transformed vertices and rebases its `visEdgeA`/`visEdgeB` by `iVertexBase` — it never evaluates
cross-placement geometry. `BuildNavAdjacency` (`NavCellData.cpp:137-204`) builds `adjOffsets`/`adjNeighbors`
from exactly those edges plus each polygon's own perimeter. So in `AStarPath`
(`Engine/Source/Frame/NavQuery.cpp:407-542`) an intermediate vertex reaches only same-island vertices, the
end node when directly visible (`:529-532`), and the start node (`:534-537`). Two islands in sequence is
unreachable.

Measured per recompute in cell `(0,0)` for a destination at the neighbour-cell centre `(0,900)`: `verts=775`,
`startVis` 20-96, `expanded=205`, and **`endVis=0`** — A\* exhausts its reachable component without any
expanded vertex ever seeing the destination. It returns zero, and `NavMissFallbackDirection`
(`NavQuery.cpp:546-577`) then steers the unit **at** the nearest start-visible obstacle vertex, a constant
`(-264.47, -234.61)`. The unit enters the polygon, the start-inside-obstacle branch (`NavQuery.cpp:649-662`)
returns an outward escape bearing, and the two form a point attractor. Terrain push never fires (measured
`push=0`, elevation ≤3.8 against threshold 4.17) because the nav polygon is inflated beyond the push contour,
so nothing physical breaks the cycle.

**Second, independent defect in the same data.** The inherited per-template edges are not merely incomplete —
they can be wrong. `BuildVisibilityGraph` validates each candidate edge against only its own template's
polygons (`NavBuild.cpp:361-369`); after the world transform (`NavCellData.cpp:236-248`) an edge internal to
island A can pass straight over island B. A\* can therefore already return a waypoint reachable only through
terrain. This is why the fix rebuilds the graph in world space rather than appending to the inherited edges.

**Provenance — not a performance regression.** The per-template-only visibility build is original design:
`BuildVisibilityGraph(NavContour&)` and its single call site are unchanged from `a07975d0` (2026-04-04)
through the current tree, and it was never once called on a merged per-cell vertex set. The gap opened
silently at `cf37bf1a` (2026-05-11, "New Gaea2 islands"), when `BuildCellNavData` began concatenating
multiple placements without re-running the visibility pass. `42000764` (2026-05-26, "Island chains")
amplified it from latent to routine by replacing 1-4 well-separated islands per cell (`kMinIslandsPerCell = 1`,
`kMaxIslandsPerCell = 4`, overlapping AABBs rejected) with a hull-adjacent chain up to
`kiMaxIslandsPerCell = 107` (`Engine/Source/Frame/IslandChainPlacement.cpp:67`). The "Perf" commit `cd8ea6d5`
(2026-05-27) is exonerated: its adjacency CSR is a neighbour-set-equivalent rewrite of a linear scan over the
same `visEdge*` arrays. It does aggravate the symptom — its 16-tick pathfind throttle holds each wrong bearing
~16.7 units, widening the oscillation into a stable limit cycle rather than a fast correction.

## Design

### 1. Whole-cell visibility graph in world space

Build one visibility graph per cell, in world space, against all of that cell's polygons, using the runtime's
own predicates so the build and query paths cannot disagree.

Promote `SegmentBlockedByObstacle` (`NavQuery.cpp:19-156`, grid-DDA broad phase) and `PointInAnyPolygon`
(`NavQuery.cpp:160-181`, per-polygon AABB broad phase) from NavQuery's anonymous namespace to `engine::`
linkage, declared in `NavBuildInternal.h` (which needs a `struct NavData;` forward declaration). That header
exists for exactly this and already carries `SegmentsIntersect` and `PointInPolygon` under the stated
rationale that a tuned epsilon or boundary rule must never drift between build and query. An edge then exists
in the graph **iff** the runtime blocked test says the segment is clear.

Delete per-template visibility entirely: `BuildVisibilityGraph` (`NavBuild.cpp:314-375`), its only callees
`SegmentIntersectsAnyEdge` (`:276-293`) and `MidpointInsideObstacle` (`:296-311`), the Step 5 call and log
(`:552-555`), and `NavContour::visEdgeA`/`visEdgeB` (`NavBuild.h:11-15`). `NavContour` is in-memory only and
never serialized, so this is not a wire change. It also removes a brute-force O(V·E) pass per template from
server boot (`IslandTerrain.cpp:227-243`, timed under `kBootTimerIslands`).

Add a file-local `BuildCellVisibilityGraph(NavData&)` in `NavCellData.cpp`, beside `BuildNavAdjacency`.
Precompute per-vertex `(polygon, localIndex, polygonCount)` in O(V), lifting the pattern from the deleted
`BuildVisibilityGraph:321-334`. Then a fixed `i < j` double loop: skip same-polygon **adjacent** pairs
(perimeter prev/next is already supplied by `BuildNavAdjacency:182-195`); `continue` when
`SegmentBlockedByObstacle` reports blocked; `continue` when `PointInAnyPolygon(midpoint)` is true, preserving
the semantics of the deleted `MidpointInsideObstacle` and catching a segment lying wholly inside a polygon,
which the blocked test alone does not. Order matters: the cheap-rejecting blocked test runs first and
early-exits on its first hit, which is the common case.

Replace the tail of `BuildCellNavData` (`NavCellData.cpp:255-267`, the rebasing loops go away) with:
`DebugCheckCrossingEdges` → `BuildNavAcceleration` → `BuildCellVisibilityGraph` → `BuildNavAdjacency`.
`BuildNavAcceleration` must run **before** the visibility pass because `SegmentBlockedByObstacle`'s documented
precondition is a populated `gridMin`/`gridMax` and edge CSR. Calling only `BuildNavAdjacency` afterwards —
not a second full `BuildNavAcceleration` — is correct and cheaper: both are file-local to this TU, and
`BuildNavAdjacency` fully rewrites `adjOffsets` and every `adjNeighbors` slot. Gate the new pass on the
existing `iVertexCount == 0` early-out.

Determinism: emission order is `(i, j)` index order over deterministic input under `/fp:strict`;
`SegmentBlockedByObstacle` is an order-independent boolean OR over grid buckets;
`BuildNavAdjacency:198-203` sorts each neighbour span; the A\* heap tie-breaks on node index. No new
determinism surface.

Computing these edges server-side and shipping them is required rather than deriving them on both sides:
`NavData::Read` ends in `BuildNavAcceleration` and runs on the client main thread inside the
allocation-tracked main loop (`Engine/Source/Network/Client/ClientReceive.cpp:288`), so an O(V²) pass there
would stall every new subscription and fight the clock servo, and it would contradict the documented
"server-built navigation is sent to clients" architecture.

### 2. A pathfinding miss must not be able to livelock

Rewrite `NavMissFallbackDirection` (`NavQuery.cpp:546-577`) to return a boundary tangent instead of a bearing
at an obstacle vertex. Take `f2Edge = NearestPolygonEdgePoint(position)` (existing helper, `:191-242`);
`outward = normalize(position - f2Edge)`, which is genuinely outward because the sole caller reaches this
only after the start-inside branch has either returned or set `f2Position = f2SnapPoint`;
`tangent = (-outward.y, outward.x)`; choose the sign by `dot(tangent, destination - position) < 0 ? -1 : +1`,
a total order and therefore deterministic; return
`normalize(tangent * sign + outward * kfNavFallbackOutwardBias)` with `kfNavFallbackOutwardBias = 0.25f`.
When `|position - f2Edge|` is degenerate, return zero and let the callers' existing straight-line fallback
run (`PlayersNavigation.cpp:319-322`, `:375-378`, `:434-438`). Drop the `pOutNextWaypoint` write from this
path so the debug waypoint stops pointing at an obstacle vertex; the default set at `NavQuery.cpp:610-613`
then stands. Guarantee, stated without overclaiming: the result carries a strictly positive component along
the outward normal of the **nearest** obstacle boundary, so it cannot drive the unit toward that boundary.

Add a `LOG(kNavData, kWarning, ...)` where `AStarPath` returns zero (`NavQuery.cpp:685`), reporting position,
destination and vertex count. After the graph fix an A\* miss means the graph is still disconnected — an
invariant violation and the decisive acceptance signal. It runs inside a tick under allocation tracking, so
it must be allocation-free using `common::WbV2`/`common::Wb`, matching `PlayersNavigation.cpp:437`.

Rekey the off-cadence pathfind recompute onto nav-polygon containment. Add
`bool XM_CALLCONV NavQueryPointBlocked(FXMVECTOR vecPosition, const NavData& rNavData)` to `NavQuery.h`, a
thin wrapper over `PointInAnyPolygon` with an empty-vertices early-out returning false and no Z assertion (it
reads x/y only). In `PlayersNavigation.cpp:274-281`, replace the `FrameElevation` probe with
`NavQueryPointBlocked` applied to **both** the lookahead point and the current position — containment at the
lookahead point does not imply containment at the position, and the livelock's steady state is a unit already
inside. Rename `kfNavTerrainLookahead` to `kfNavLookahead` (`:33`) and rewrite the `:267-273` comment block to
describe the containment trigger. The old probe tested the **inner** contour: `Engine/Source/Main.cpp:252`
passes `gBaseHeight.Get() - game::kfPlayerRadius - game::kfPushMargin` as the nav threshold and
`ApplyTerrainPush` (`PlayersNavigation.cpp:472`) uses that same expression, while the nav polygon is that
contour inflated outward — which is exactly why `push=0` while a unit sits in the no-nav band. Keep
`kiNavRecomputeInterval = 16` (`:27`), the `riNavDirection >= 0` guard, and the mode `-1` roam exemption. The
change adds no RNG draw, preserving the invariant that random draws stay outside the pathfinding throttle.

### 3. Version bump

Bump `engine::kiNavDataVersion` 12 → 13 (`NavBuild.h:17`). Pathfinding output changes `rVecAiDirection`,
which moves units, and positions are in the shared CRC; `Frame.cpp:33-35` mandates a bump whenever computed
frame CRCs shift, and `kiNavDataVersion` is the designated summand at `Frame.cpp:36`. Do **not** also
increment the `121` base — that is reserved for CRC-semantics changes with no contributing version bump, so
`Frame.cpp` is not edited by this plan.

Accepted consequence: every existing quicksave and replay stops loading
(`Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp:549-553`), and client and server must be rebuilt
and deployed together because the handshake verifies the frame version
(`Engine/Source/Network/Server/ServerReceive.cpp:270-281`). No backward compatibility is added.

### 4. Build cost

The new pass is O(V²) at cell scale — 775 vertices measured in cell `(0,0)`, worst case on the order of 2000 —
on the per-coord dispatch thread, where `BuildCellNavData` is currently only O(V) copying. Land the
**complete** pass, plus one `LOG(kNavData, kInfo, ...)` at the end of `BuildCellNavData` reporting placement
count, vertex count, polygon count, visibility-edge count, `8 * visEdgeA.size()` wire bytes, and elapsed
microseconds. It is one-shot per cell and is the instrument the cost and wire-size acceptance criteria depend
on, so it lands rather than being removed.

Both contingency levers are pre-authorised inside this change; apply in order only when an acceptance budget
fails, and re-measure after each:

1. Raise `kiNavZonesX`/`kiNavZonesY` 16 → 32 → 64 (`NavBuild.h:19-22`). Purely derived, non-serialized,
   recomputed identically on both sides, zero version impact; `iMaxSteps` at `NavQuery.cpp:119` scales
   automatically. Helps build and query cost together.
2. Restrict visibility candidates in `BuildCellVisibilityGraph` to vertices convex on their own obstacle. A
   shortest obstacle-avoiding path bends only at convex obstacle vertices, so every optimal path survives, and
   the full polygon perimeter chain remains in adjacency (`BuildNavAdjacency:182-195`) so the graph cannot
   disconnect. Depends on the CCW winding invariant already asserted at `NavBuild.cpp:549`. Comment the
   taut-string justification at the pruning site.

A per-cell Clipper2 `Union` was evaluated and rejected: `IslandChainPlacement.cpp:271-291` leaves
`kfTouchGapMeters = 5.0f` between convex hulls and rejects any placement where `ConvexHullsOverlap`, and the
nav contour is inset inside the shoreline which is inset inside the hull, so the polygons very likely do not
overlap and a union would merge nothing. It also carries an int64 quantisation hazard (`PathsD` at
`kiClipperPrecision = 6` on an unbounded sparse world grid, safe today only because the existing use runs in
UV [0,1]²) and its `Area <= 0` hole-drop would convert enclosed inter-island water into solid obstacle.

### 5. Allocation tracking on the second build call site

`ServerSession::PreparePausedSubscriptions`
(`Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp:325-329`) calls `BuildCellNavData`
without `ScopedSuppressAllocationTracking`, unlike `FrameTick.cpp:35`. It runs on the main thread inside the
armed main loop, so each `push_back` is a live `DEBUG_BREAK()`. This is pre-existing, but the change raises
allocation volume on that exact path by an order of magnitude, so fix it here: add the guard with a `// Heap:`
rationale mirroring `FrameTick.cpp:35`.

## Scope contract

The regions named below are both target and ceiling. Make the smallest complete change inside them and add no
abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file
grants no permission to touch anything in it beyond the named regions plus the mechanical necessities
(includes, declarations) the named change requires.

**In scope:**

- `Engine/Source/Frame/NavCellData.cpp` — `BuildCellNavData` (`:208-268`): delete the `visEdgeA`/`visEdgeB`
  rebasing loops (`:255-262`) and replace the tail call sequence (`:265-267`); new file-local
  `BuildCellVisibilityGraph` in the existing anonymous namespace beside `BuildNavAdjacency`; the new
  `kNavData` cost/size log line at the end of `BuildCellNavData`; and the `DebugCheckCrossingEdges` comment
  (`:13-16`), which asserts overlapping placements mean the pathfinder will misbehave — cross-placement
  overlap is now handled by the world-space predicates. No change to `BuildNavAdjacency`'s body
  (`:137-204`), `BuildNavAcceleration` (`:270-335`), or `NavData::Write`/`Read` (`:337-401`).
- `Engine/Source/Frame/NavQuery.cpp` — move `SegmentBlockedByObstacle` (`:19-156`) and `PointInAnyPolygon`
  (`:160-181`) out of the anonymous namespace to `engine::` linkage, bodies unchanged; rewrite
  `NavMissFallbackDirection` (`:546-577`) and its call site (`:684-688`); add the A\*-miss `kWarning` at
  `:685`; add the `NavQueryPointBlocked` definition. No change to `AStarPath` (`:407-542`), the
  start-inside-obstacle branch (`:649-662`), `NearestPolygonEdgePoint` (`:191-242`), `SnapOutsidePolygon`
  (`:245-258`), or `NavQuerySnapToNavigable` (`:581-599`).
- `Engine/Source/Frame/NavBuildInternal.h` — add the `struct NavData;` forward declaration and the two
  predicate declarations alongside the existing pair (`:22-28`).
- `Engine/Source/Frame/NavQuery.h` — add the `NavQueryPointBlocked` declaration beside the existing two entry
  points (`:8-9`).
- `Engine/Source/Frame/NavBuild.h` — `kiNavDataVersion` 12 → 13 (`:17`); remove `visEdgeA`/`visEdgeB` from
  `NavContour` (`:11-15`); update the serialized-content comment (`:40-48`) to state that `visEdgeA`/
  `visEdgeB` are now the whole-cell world-space graph produced by `BuildCellNavData`; `kiNavZonesX`/
  `kiNavZonesY` (`:19-22`) only under lever 1.
- `Engine/Source/Frame/NavBuild.cpp` — deletions only: `SegmentIntersectsAnyEdge` (`:276-293`),
  `MidpointInsideObstacle` (`:296-311`), `BuildVisibilityGraph` (`:314-375`), and the Step 5 call and log
  (`:552-555`). No other change to `BuildNavContour`, including its Clipper2 block (`:466-500`) and the CCW
  assertion (`:536-549`).
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersNavigation.cpp` — the
  `kfNavTerrainLookahead` constant and its comment (`:29-33`) and the off-cadence recompute probe and comment
  block (`:267-281`). No change to the fleet-override block (`:172-212`), the flagship-proximity block
  (`:214-247`), the mode-4/5 branches, `ApplyTerrainPush` (`:468-480`), or `kiNavRecomputeInterval` (`:27`).
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — add
  `ScopedSuppressAllocationTracking` with a `// Heap:` rationale inside `PreparePausedSubscriptions`
  (`:325-329`) only.
- `Engine/Source/Frame/AGENTS.md` — the Terrain and Navigation bullets: the visibility graph is whole-cell and
  world-space, built server-side in `BuildCellNavData`; templates carry contour geometry only.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/AGENTS.md` — the Navigation Invariants bullet
  covering the off-cadence recompute trigger.

**Out of scope:**

- The start-inside-polygon escape branch (`NavQuery.cpp:649-662`). It is the other arm of the observed cycle
  but is correct behaviour for a genuinely enclosed start; revisit only if acceptance still shows in/out
  oscillation, as a separate plan.
- `kfSnapOffset` (`NavQuery.cpp:247`), `kfSimplifyEpsilon` and the nav threshold, marching-squares contour
  extraction, and interior-hole handling.
- **`kfInflateDelta = 0.015` being UV-relative** (`NavBuild.cpp:470`): it scales by each template's
  `mfQuadFootprintX/Y`, so the no-nav margin is anisotropic, island-size dependent, and unrelated to
  `kfPlayerRadius + kfPushMargin`. A real separate defect surfaced by this investigation — route to
  `/create-follow-up-plans`. Changing obstacle geometry here would confound this change's acceptance signal.
- `kiNavRecomputeInterval`'s value; any A\* algorithm change beyond the miss fallback; per-cell Clipper2
  `Union`; LZ4-compressing the static-data message; Spaceships navigation, which does not use NavQuery.
- `Frame.cpp` entirely, including the `121` base (`:36`); `NavData::Write`/`Read` field list and its
  `ValidateDeserializedCount` trust-boundary guards — only the *content* of `visEdgeA`/`visEdgeB` changes, not
  the wire layout.
- `.vcxproj` and filter membership: no file is added, removed, or affinity-changed, so `/update-vcxproj` is
  untriggered.
- Regenerating or tracking a replacement `ServerQuicksave.save`.

## Critical files

- `Engine/Source/Frame/NavCellData.cpp` — the merge and acceleration domain; the core change.
- `Engine/Source/Frame/NavQuery.cpp` — predicate promotion, miss fallback, new point query.
- `Engine/Source/Frame/NavBuild.cpp` / `NavBuild.h` — per-template visibility deletion and the version bump.
- `Engine/Source/Frame/NavBuildInternal.h` — the shared-predicate seam this change extends.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersNavigation.cpp` — the sole
  `NavQueryDirection` consumer and the throttle.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — the second `BuildCellNavData` call
  site.

## Risk tier and invariants

**Tier 3.** Triggers: determinism/CRC (path output feeds CRC'd unit positions), save/replay compatibility
(`Frame::kiVersion` shift via `kiNavDataVersion`), serialization semantics and wire-payload growth on the
static-data message, threading (build runs on the per-coord dispatch thread, plus a second call site corrected
for allocation tracking), and the change spans independently owned subsystems (engine frame navigation, game
player navigation, game server session).

Invariants to preserve: navigation data stays outside the shared CRC and outside the persisted save payload;
nav data remains server-built and wire-shipped, with only the derived acceleration rebuilt on both sides;
visibility-edge emission order and neighbour-span sorting remain index-deterministic; no RNG draw is added or
removed in `ComputeNavigation`; `NavData::Read` keeps its `ValidateDeserializedCount` trust-boundary guards.

## Acceptance criteria

1. No A\*-miss `kNavData` warning and no `Player {} navQuery returned zero` line
   (`PlayersNavigation.cpp:437`) over at least 1000 ticks with at least 8 players in cell `(0,0)`.
2. Tracked units achieve at least 500 units of net displacement over 1000 ticks against a measured pre-change
   baseline of ~4, and appear in `query_players {"coord":[0,1]}` within the run.
3. Units that already navigated correctly still reach a neighbour cell, and no new terrain-stuck unit appears
   over a 2000-tick run.
4. At least 2000 ticks of live client plus server with zero `CONFIRMED DESYNC after full rollback/replay`.
5. Replay determinism acceptance passes per `Projects/BrokenEngineSandbox/Documents/AgentHarness.md`: no
   newly appended `LogDifferences CRC Client`, no checksum mismatch, and an appended
   `End replay <tick>, looping`.
6. Maximum `BuildCellNavData` elapsed time across at least 20 distinct cells is at most 100 ms in a Debug
   build; otherwise apply the section-4 levers in order and re-measure.
7. Maximum `8 * visEdgeA.size()` is at most 256 KB per cell, since nine cells of the 3x3 ring are sent on
   subscribe.
8. `kCpuTimerPostRenderUpdateNavQuery` average stays within budget, compared using equal-sample cohorts
   captured in one process lifetime after a discarded warm-up cohort.
9. Debug build: connecting a client while the server is **paused**, which exercises
   `PreparePausedSubscriptions`, produces no `DEBUG_BREAK`.
10. A freshly written quicksave round-trips at the new version, and a pre-change save is rejected with the
    expected `ReadGrid ... version` error rather than a crash.
11. `/compile` is clean for both the client and the server.

Criteria 1, 4, 5, 9 and 11 are non-negotiable gates; 2 and 3 are the user-visible fix; 6, 7 and 8 govern the
contingency levers.

## Verification

Runtime verification through `/agent-harness` on a Debug build, where `kbDebugRender` is enabled and the nav
overlay is available. **Sequencing is load-bearing: the version bump invalidates the repro save, so capture
the baseline before making any edit.**

Baseline, on the unmodified tree: load the existing quicksave, confirm cell `(0,0)` is live, and record
`query_players {"coord":[0,0]}` positions for the stuck units at T, T+250, T+500, T+750 and T+1000; also
capture the baseline `query_profile` NavQuery average after discarding a warm-up cohort.

Then build a procedural repro that survives the bump, and record the same displacement table from it. Island
placement is seeded from the grid coord alone (`GenerateIslandChain`, `IslandChainPlacement.cpp:317-321`), so
cell `(0,0)`'s obstacle field is identical in a fresh game: `reset`, then `inject_status_changes` with four
`SpawnPlayer` entries at `coord:[0,0]`, one with `isFlagship:true` and all with `fleetWantedCoord:[0,1]`,
which places them in nav mode 0 toward `(0,900)` — the exact repro geometry. This procedural table is the
comparable baseline.

After the change, re-run the procedural scenario verbatim and compare the displacement table for criteria 2
and 3; read the A\*-miss and cost lines with `get_logs {"category":"NavData"}` for criteria 1, 6 and 7; use
`query_profile` for criterion 8; exercise the paused-subscription connect for criterion 9; round-trip a save
for criterion 10; and run replay acceptance for criterion 5. Note that `DebugRenderNavData`
(`Engine/Source/Graphics/Render/MainUniforms.cpp:142-184`) draws polygon perimeters and vertex markers only,
not visibility edges, so use `pVecDebugNavWaypoints` to confirm waypoints now advance along a route instead of
pinning to a single obstacle vertex.

## Coordination

- This plan owns its own `kiNavDataVersion` increment and lands independently; existing quicksaves and
  replays are invalidated at that landing, which is accepted. It shifts `game::Frame::kiVersion` through the
  `engine::kiNavDataVersion` summand (`Frame.cpp:36`) without editing `Frame.cpp`, so members of the frame
  version/save/replay batch named in `Documents/Plans/Frame/PromotedFlagshipNavStall.md` should treat the
  invalidation as already spent once this lands rather than expecting a co-land.
- `Documents/Plans/Frame/PromotedFlagshipNavStall.md` also edits
  `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersNavigation.cpp`, but in disjoint
  regions of `ComputeNavigation` — its flagship-proximity block (`:214-247`) and mode-5 comment (`:285-296`)
  against this plan's constant (`:29-33`) and off-cadence recompute probe (`:267-281`). Either order lands
  cleanly; expect a textual rebase only if both touch the surrounding comment blocks.
