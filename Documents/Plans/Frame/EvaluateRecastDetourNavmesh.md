# Evaluate Recast/Detour as a navmesh replacement for the visibility-graph pathfinder

## Context

The current per-cell pathfinder is a hand-rolled **visibility-graph A\*** over island-contour polygons. The
serialized graph (`engine::NavData` in `Engine/Source/Frame/NavBuild.h`) is built server-side by
`engine::BuildCellNavData` (rebasing each placement's template `NavContour` into world space) and queried at
runtime through `engine::NavQueryDirection` / `engine::NavQuerySnapToNavigable`
(`Engine/Source/Frame/NavQuery.h`), which fast-path direct-LOS and fall back to the internal `AStarPath`
(`NavQuery.cpp:366`) over the per-vertex adjacency CSR.

A broad-phase optimization just landed (edge grid + AABB rejects in `BuildNavAcceleration`, A\* heap +
adjacency CSR in `NavQuery.cpp`, and a per-player pathfinding throttle in
`Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersNavigation.cpp`). With that in place the
in-house pathfinder should be fast enough — but we agreed ("Both") to *separately* evaluate replacing it with
the industry-standard **Recast/Detour** navmesh system (zlib license) once we can measure against the optimized
baseline.

This plan is the **evaluation / spike**, not a commitment to integrate. It produces a written go/no-go, not a
diff.

## Design

This is an evaluation: enumerate what to assess, not a build to execute.

(a) **License fit.** Recast/Detour is zlib-licensed — commercial-friendly, no copyleft, no attribution burden
beyond the notice. Confirm the version we'd vendor carries the zlib terms and nothing has been relicensed.

(b) **Determinism risk — THE GATING CONCERN.** Detour is floating-point and is **not** guaranteed
bit-stable. Our simulation is **deterministic lockstep**: client and server run the same physics from the same
inputs and validate via a shared CRC; `NavData` is serialized over the wire and feeds `Frame::kiVersion`.
Pathfinding feeds steering (`rVecAiDirection` in `ComputeNavigation`), which moves units, which enters the CRC.
Assess whether Detour query results can stay **bit-identical across client and server** given: same binary,
same platform (x64), `/fp:strict`, FMA3 disabled, SSE4.1 (the existing determinism contract — see
`Documents/FloatingPointDeterminism.txt`). Specifically inspect Detour's query internals (`dtNavMeshQuery`
A\*, funnel/string-pulling, `dtVlerp`/area-cost math) for ordering hazards, `std::sort` on float keys,
fast-inverse-sqrt, or `dtMathSqrtf` paths that could diverge. If Detour can desync, this is a **no-go** unless
it can be confined to a non-CRC path (e.g., client-only display steering with server authority) — which our
shared-CRC steering model does not currently allow.

(c) **Tiled-navmesh fit to our layout.** Our world is a sparse grid of 1000x1000-unit cells; each cell's
immutable nav state lives in `engine::FrameStaticData::navData`. Detour's tiled navmesh maps naturally — one
Detour tile per cell, built from the same per-island contours `BuildCellNavData` already composites. Assess
whether per-cell tile build + serialize replaces our `NavData::Write`/`Read` cleanly, and whether the version
gate (`engine::kiNavDataVersion`) generalizes to a tile blob.

(d) **Integration cost (estimate only, do not build).** Vendoring under `ThirdParty/` (Recast + Detour +
DetourTileCache as needed); build wiring in `ThirdParty.vcxproj` + filters; namespace isolation from `engine::`;
which call sites swap — `engine::NavQueryDirection` and `engine::NavQuerySnapToNavigable` (query side) and
`engine::BuildCellNavData` (build side), plus `NavData` serialization. Note the broad-phase acceleration
(`BuildNavAcceleration`, edge grid / adjacency CSR) becomes dead if Detour owns querying.

(e) **Gain vs. our minimal needs.** Recast/Detour brings dynamic obstacles (TileCache), off-mesh links
(jumps/teleports), agent radius/clearance baking, and crowd avoidance. Weigh those against what we actually
use: static per-cell obstacle avoidance toward a destination, no dynamic obstacles, no agent-radius baking
today. If we use ~10% of the library, the determinism risk + integration cost likely outweigh the gain.

(f) **Measure first.** Before recommending anything, measure the *optimized* in-house pathfinder
(`kCpuTimerPostRenderUpdateNavQuery` already brackets the `NavQueryDirection` calls in `ComputeNavigation`;
add a build-time timer around `BuildCellNavData` if needed) on a worst-case multi-island archipelago cell.
Only recommend proceeding toward integration if it is **still** a measured bottleneck after the throttle +
broad-phase work.

## Critical files

Reference points for the evaluation (no edits land in this plan):

- `Engine/Source/Frame/NavBuild.h` / `NavBuild.cpp` — `engine::NavData` layout, `engine::BuildCellNavData`,
  `engine::BuildNavAcceleration`, `engine::kiNavDataVersion`. The build + serialize side a Detour tile would
  replace.
- `Engine/Source/Frame/NavQuery.h` / `NavQuery.cpp` — `engine::NavQueryDirection`,
  `engine::NavQuerySnapToNavigable`, internal `AStarPath`. The query side a `dtNavMeshQuery` would replace.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersNavigation.cpp` —
  `PlayersPostRender::ComputeNavigation`, the sole gameplay consumer of the query API and the profiling site.
- `Documents/FloatingPointDeterminism.txt` — the determinism contract any replacement must satisfy.
- `ThirdParty/` (+ `ThirdParty.vcxproj`/filters) — where the library would vendor *if* go.

## Out of scope

- **Actually integrating the library** — vendoring, build wiring, call-site swaps. Those are a follow-up plan
  only if this evaluation returns "go".
- Any **client/server protocol change** or `NavData` wire-format migration.
- Changing the existing visibility-graph pathfinder, the broad-phase acceleration, or the per-player throttle.
- Crowd/avoidance, dynamic obstacles, off-mesh links as *features* — only assessed as potential gain, not
  designed here.

## Acceptance criteria

- A written **go/no-go recommendation** backed by:
  - a **determinism assessment** of Detour's query path against our lockstep/`/fp:strict`/shared-CRC contract
    (item (b)) — the single deciding factor;
  - a **measured comparison** of the optimized in-house pathfinder (query + build) against our worst-case
    multi-island cell (item (f)), establishing whether a replacement is even warranted.
- If "go": a one-paragraph integration sketch naming the swap sites (the four interfaces above) and the
  estimated effort tier, handed to a follow-up integration plan. If "no-go": the recorded reason so the
  question is not re-opened without new evidence.

## Notes

- Scores as a research/eval spike, not an implementation: **Effort 3** (Medium — library study +
  determinism source review + measurement, but no code lands), **Impact 2** (modest; mostly informs a future
  decision rather than fixing a present problem), **Risks 0** (pure evaluation, nothing ships). Score =
  3 − 2 + 0 = **1**, tier **Medium**.
- The determinism gate is decisive: our steering feeds the shared CRC, so a non-bit-stable query is a desync
  source, not a tunable. Treat any "probably deterministic" finding as no-go until proven bit-identical.
- Pairs with `Frame/NavBuildDebugCrossingCheckPerf.md` only insofar as both touch the nav files; they are
  independent — one evaluates a replacement, the other tidies the existing builder.
