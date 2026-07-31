<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-26T18:56:26.344Z","dependsOn":[]} -->
# Nav Obstacle Inflation Margin Is UV-Relative, Not A Ship Clearance

## Context

The no-navigation margin around every island is specified in **UV space**, so the world-space clearance it
produces is anisotropic, island-size dependent, and unrelated to how much room a ship actually needs.

`static constexpr double kfInflateDelta = 0.015` (`Engine/Source/Frame/NavBuild.cpp:368`) is passed to
`Clipper2Lib::InflatePaths` at `NavBuild.cpp:396`, inside `BuildNavContour` (`:342`). That whole Clipper2
block operates on the marching-squares contour in normalized template UV space `[0,1]^2` — `BuildNavContour`'s
signature (`Engine/Source/Frame/NavBuild.h:64`) takes a heightmap and a world height threshold and has no
access to the template's footprint at all. The contour is baked once per island **template** at server boot
(`Engine/Source/Frame/IslandTerrain.cpp:227-243`, `BT_SERVER` only).

UV becomes world only later, per placement, in `BuildCellNavData`
(`Engine/Source/Frame/NavCellData.cpp:333-334, 342-343`):

```
fLocalX = (fU - 0.5f) * rTemplate.mfQuadFootprintX;
fLocalY = (0.5f - fV) * rTemplate.mfQuadFootprintY;
```

`mfQuadFootprintX`/`mfQuadFootprintY` are the template's world footprint in meters
(`IslandTerrain.cpp:44-45`, assigned from `mfWorldFootprintXMeters`/`YMeters`; declared
`IslandTerrain.h:62-63`). Three consequences follow directly:

1. **Anisotropic.** A UV delta of `0.015` becomes `0.015 * mfQuadFootprintX` world units along X and
   `0.015 * mfQuadFootprintY` along Y. For any template whose footprint is not square the margin differs per
   axis, and the miter offset computed in UV is not a true offset curve in world space at all — it is an
   affine (non-uniformly scaled) image of one, so even corner geometry is distorted.
2. **Island-size dependent.** A template with a 400-unit footprint gets `0.015 * 400 = 6.0` units of
   inflation on that axis; a template a quarter that size gets `1.5`. Bigger islands are surrounded by a
   proportionally bigger no-navigation band for no navigational reason.
3. **Unrelated to the clearance a ship needs.** What actually determines required clearance is
   `game::kfPlayerRadius = 1.1f`
   (`Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.h:24`) plus
   `game::kfPushMargin = kfPlayerRadius * 0.6667f` (`:27`) — the same expression the runtime uses as the nav
   elevation threshold at `Engine/Source/Main.cpp:252` and `:307`
   (`gBaseHeight.Get() - game::kfPlayerRadius - game::kfPushMargin`) and that `ApplyTerrainPush` reuses. Sum:
   roughly `1.83` world units. `kfInflateDelta` is disconnected from that number and from any other
   ship dimension; on a 400-unit island it over-reserves by more than 3x, and on a small island it may
   under-reserve.

Surfaced by, and explicitly routed out of, the whole-cell world-space visibility graph change (landed
2026-07-26), which named it a real separate defect and excluded it because changing obstacle geometry mid-fix
would have confounded that change's acceptance signal.

Symptom exposure, stated without overclaiming: the inflated polygon is what
`NavQueryPointBlocked`/`PointInAnyPolygon` test and what the visibility graph routes around. An over-wide band
pushes routes further out than needed, keeps ships "inside an obstacle" over water they could legally occupy,
and — because the nav polygon is inflated beyond the terrain push contour — creates the band in which terrain
push cannot fire while nav considers the position blocked. No measurement of that gap is claimed here; it is
the reason the defect matters, not evidence of a specific failure.

## Design

Make the no-navigation margin a **world-space** quantity derived from ship clearance rather than a UV
constant.

Two shapes are visible from the code, with materially different cost and layering. **Do not pick one from
inspection — evaluate both against the criteria below before implementing.**

- **A: keep inflation in the template bake, plumb the footprint in.** Widen `BuildNavContour`
  (`NavBuild.h:64`) so it receives the template's footprint, and convert a world-space margin into the
  per-axis UV deltas it needs. Cost: Clipper2 offsets by one scalar delta, so a per-axis margin is not
  directly expressible — a uniform UV delta cannot produce a uniform world margin on a non-square footprint,
  which is the defect. Any variant of A must state how it resolves that, e.g. by scaling the contour to world
  proportions before offsetting.
- **B: move inflation to the world-space cell merge.** Inflate in `BuildCellNavData`
  (`NavCellData.cpp`) after the UV -> world transform, where a single world-space delta is already correct and
  isotropic and rotation is already applied. Cost: the offset runs per cell per placement instead of once per
  template at boot, and it lands on the per-coord dispatch thread rather than server boot. Also carries the
  int64 quantization hazard that the landed change recorded against a per-cell Clipper2 pass — `PathsD` at a
  fixed precision over an unbounded sparse world grid, safe today only because the existing use runs in UV
  `[0,1]^2`. Quantify both before choosing.

Under either shape the margin's value comes from `kfPlayerRadius + kfPushMargin`, not from a bare literal, and
the site carries a comment stating that the nav polygon is deliberately the terrain-push contour plus ship
clearance.

## Critical files

- `Engine/Source/Frame/NavBuild.cpp` — `BuildNavContour` (`:342`), the Clipper2 Union/Inflate/Simplify block
  (`:364-398`), `kfInflateDelta` (`:368`), `kfMiterLimit` (`:369`), `kiClipperPrecision` (`:374`).
- `Engine/Source/Frame/NavBuild.h` — `BuildNavContour`'s signature (`:64`) and `kiNavDataVersion` (`:15`).
- `Engine/Source/Frame/NavCellData.cpp` — the UV -> world transform (`:333-334`, `:342-343`) and
  `BuildCellNavData`, the only place the footprint and the contour meet.
- `Engine/Source/Frame/IslandTerrain.cpp` — the per-template bake loop (`:227-243`) and the footprint
  assignment (`:44-45`); `IslandTerrain.h:62-63` for the footprint members.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.h` — `kfPlayerRadius` (`:24`) and
  `kfPushMargin` (`:27`), the clearance the margin must express.
- `Engine/Source/Main.cpp` — the nav elevation threshold (`:252`, `:307`), which fixes the contour the
  inflation starts from.

## In scope

- `Engine/Source/Frame/NavBuild.cpp` — `kfInflateDelta` (`:368`) and the
  `Clipper2Lib::InflatePaths` call (`:396`) inside `BuildNavContour` (`:342`):
  the no-navigation margin becomes a world-space quantity derived in code from
  `game::kfPlayerRadius + game::kfPushMargin`, with no bare inflation literal
  left, and the site carries a comment stating that the nav polygon is
  deliberately the terrain-push contour plus ship clearance.
- Whichever of shape A or B the required evaluation selects, and only that one:
  - A — `Engine/Source/Frame/NavBuild.h` `BuildNavContour`'s signature (`:64`)
    widened to receive the template footprint, with the per-template bake call
    site in `Engine/Source/Frame/IslandTerrain.cpp` (`:227-243`) and the
    footprint members (`IslandTerrain.h:62-63`, assigned at
    `IslandTerrain.cpp:44-45`) plumbed in, plus the stated resolution of
    Clipper2's single-scalar delta on a non-square footprint.
  - B — the inflation moved into `BuildCellNavData`
    (`Engine/Source/Frame/NavCellData.cpp`) after the UV -> world transform
    (`:333-334`, `:342-343`), including the int64 quantization question, which
    may change `kiClipperPrecision` (`NavBuild.cpp:374`) only with the reasoning
    recorded.
- `Engine/Source/Frame/NavBuild.h` — this Plan's own `kiNavDataVersion` (`:15`,
  currently 13) increment. No backward compatibility is added.
- Read-only reference for the clearance value and the contour the inflation
  starts from: `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.h`
  (`kfPlayerRadius` `:24`, `kfPushMargin` `:27`) and `Engine/Source/Main.cpp`
  (`:252`, `:307`).

## Out of scope

- `kfSimplifyEpsilon` (`NavBuild.cpp:370`), `kiClipperPrecision` (`:374`), and `kfMiterLimit` (`:369`) except
  where shape B's world-space quantization forces the precision question — in which case change precision only,
  with the reasoning recorded.
- Marching-squares contour extraction, edge chaining, hole handling, and the `Area <= 0` hole drop.
- The nav elevation threshold expression itself (`Main.cpp:252`, `:307`) and `ApplyTerrainPush`. This Plan
  changes how far outside that contour navigation is forbidden, not where the contour is.
- The visibility graph build, `AStarPath`, and the runtime query predicates — they consume whatever polygons
  they are given.
- NavQuery per-tick cost — owned by `Documents/Plans/Frame/NavQueryPerTickPathfindCost.md`.
- Per-unit-radius clearance (different margins for different ship sizes). One margin for all units; making it
  per-unit is an architectural change to nav data, not this defect.
- `game::Frame::kiVersion`'s own base in `Frame.cpp` — this Plan bumps `kiNavDataVersion` only and does not
  edit `Frame.cpp`.

## Risk tier and invariants

**Tier 3.** Triggers: determinism/CRC — obstacle geometry changes which bearing `NavQueryDirection` returns,
which moves unit positions, and positions are in the shared CRC; save/replay compatibility — that shift
requires bumping `engine::kiNavDataVersion` (`NavBuild.h:15`, currently 13), which is a summand of
`game::Frame::kiVersion`, invalidating every existing quicksave and replay and requiring client and server to
be rebuilt and deployed together; serialization semantics — the shipped `NavData` vertex and polygon content
changes; and, under shape B only, threading — the offset would move onto the per-coord dispatch thread.

**This Plan owns its own `kiNavDataVersion` bump.** No backward compatibility is added.

Invariants to preserve: navigation data stays server-built and wire-shipped, with only derived acceleration
rebuilt on both sides; nav data stays outside the shared CRC and outside the persisted save payload;
`NavData::Read` keeps its `ValidateDeserializedCount` trust-boundary guards; polygon emission order stays
index-deterministic under `/fp:strict`; world-space nav polygons remain clockwise (the UV -> world mapping
mirrors Y), so no orientation-sensitive test may assume the template's counter-clockwise UV winding; the
inflated polygon must remain strictly outside the terrain-push contour, or a ship can be pushed by terrain in
water that navigation considers legal.

## Acceptance criteria

1. For at least two templates with **different** footprint sizes, the measured world-space distance from the
   pre-inflation contour to the inflated nav polygon is within a stated tolerance of the same target value on
   both — no longer proportional to footprint.
2. For at least one template with a non-square footprint (`mfQuadFootprintX != mfQuadFootprintY`), that
   measured distance agrees between the X and Y axes within the same tolerance. If no shipped template has a
   non-square footprint, record that finding and demonstrate axis independence on a synthetic footprint
   instead.
3. The target margin is derived in code from `kfPlayerRadius + kfPushMargin`, with no remaining bare
   inflation literal.
4. The inflated nav polygon still fully contains the terrain-push contour for every shipped template: no
   sampled position exists where `ApplyTerrainPush` fires while `NavQueryPointBlocked` returns false.
5. Zero A\*-miss `kNavData` warnings (`Engine/Source/Frame/NavQuery.cpp:703`) over at least 1000 ticks with at
   least 8 units in cell `(0,0)`, and every unit either reaches a neighbour cell or shows at least 500 units
   of net displacement over the 1000-tick window — the whole-cell graph fix survives the geometry change.
6. At least 2000 ticks of live client plus server with zero `CONFIRMED DESYNC after full rollback/replay`, and
   replay determinism acceptance passes.
7. `kiNavDataVersion` is bumped; a freshly written quicksave round-trips at the new version and a pre-change
   save is rejected with the expected version error rather than crashing.
8. Per-cell `BuildCellNavData` elapsed time stays within its landed budget of 100 ms across at least 20
   distinct cells in a Debug build — the criterion that decides against shape B if world-space offsetting is
   too expensive.
9. `/compile` is clean for both the client and the server.

## Verification

Runtime through `/agent-harness` on a Debug build, where the nav overlay is available: `DebugRenderNavData`
(`Engine/Source/Graphics/Render/MainUniforms.cpp`) draws polygon perimeters and vertex markers, which is the
visual check for criteria 1 and 2 alongside numeric comparison of pre- and post-inflation vertices logged from
the build. Use the per-cell `kNavData` cost log emitted at the end of `BuildCellNavData` for criterion 8 and
`get_logs {"category":"NavData"}` for criterion 5. Capture a pre-change quicksave before making any edit —
the version bump invalidates it and criterion 7 needs one.

## Coordination

- `Documents/Plans/Frame/NavQueryPerTickPathfindCost.md` measures per-tick `NavQuery` cost against a baseline
  that this Plan's geometry change moves: it changes obstacle vertex counts (what A\* iterates) and the
  containment area that fires the off-cadence pathfind recompute. Whichever of the two Plans lands second must
  re-establish its baseline against the landed tree before claiming a measurement; do not carry a number
  across that landing. There is no required order and no co-land requirement.
- This Plan owns its own `kiNavDataVersion` increment and lands independently. It shifts
  `game::Frame::kiVersion` through the `engine::kiNavDataVersion` summand without editing `Frame.cpp`, so
  members of the frame version/save/replay batch named in
  `Documents/Plans/Frame/PromotedFlagshipNavStall.md` should treat save and replay invalidation as already
  spent once this lands, rather than expecting a co-land.
