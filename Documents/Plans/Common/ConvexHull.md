# ConvexHull Determinism: Remove Transcendentals From `BuildWorldHull` (CRC Path)

## Context

`Common/Math/ConvexHull.h` implements 2D convex-hull collision (SAT broadphase + narrowphase)
for island-chain placement. It is live, not orphaned: it is aggregated through `Common/Common.h:18`
and listed in `DataPacker.vcxproj` plus both Sandbox client/server vcxprojs.

Real consumers (verified):
- `Engine/Source/Frame/IslandChainPlacement.cpp` calls `common::BuildWorldHull` at lines 225 and
  271 and `common::ConvexHullsOverlap` at line 284, to pack islands by their true valid-area hull.
- `DataPacker/Source/ExportJobs/ExportIsland.cpp:170` documents that the runtime SAT
  (`common::ConvexHullsOverlap`) requires both inputs convex + CCW and validates that at bake.

`BuildWorldHull` (`Common/Math/ConvexHull.h:21`) rotates a CCW island-local hull into world space
using `std::cos(fRotation)` / `std::sin(fRotation)` (`Common/Math/ConvexHull.h:23-24`). The
project's determinism rule is explicit: `Documents/FloatingPointDeterminism.txt:77` —
"transcendental functions are known to differ across implementations; avoiding them eliminates
this class of non-determinism entirely." The project-wide `/fp:strict` + `_set_FMA3_enable(0)` +
per-thread MXCSR regime constrains basic IEEE ops but does NOT make libm `std::cos`/`std::sin`
bit-identical across toolchains/platforms.

`IslandChainPlacement` runs island-chain layout that feeds island spawns and CRC verification, so
two machines with differing libm `cos`/`sin` rounding can produce different world-hull vertices ->
different SAT results -> different chain layout -> CRC desync. This is the single highest-value
finding in the source report (report H-1) and the only one that survived validation. Note the
placement consumer itself already calls `std::cos`/`std::sin` directly
(`IslandChainPlacement.cpp:203-204, 242-243, 246-247) — those are the same class of violation and
should be addressed by the same deterministic-rotation decision, not left behind.

## Design

Replace the transcendental rotation source so the `(cos, sin)` pair used for hull rotation is
deterministic across platforms. `BuildWorldHull` already takes the angle only to derive `(cos, sin)`.

Preferred approach (KISS): **pass precomputed `fCos`/`fSin` into `BuildWorldHull`**, sourced from a
single deterministic rotation builder shared with the placement consumer.

- Change `BuildWorldHull` (`Common/Math/ConvexHull.h:21`) to accept `float fCos, float fSin` in
  place of `float fRotation`, and delete the `std::cos`/`std::sin` calls
  (`Common/Math/ConvexHull.h:23-24`). The rotate body at lines 32-33 already consumes `fCos`/`fSin`
  unchanged. [effort: S]
- Update the two call sites `IslandChainPlacement.cpp:225` and `:271` to pass the deterministic
  `(cos, sin)`. The placement code already computes `fCos`/`fSin` at lines 246-247 (and the
  footprint estimate at 203-204, direction at 242-243) — route all of these through one
  deterministic source so the value feeding `BuildWorldHull` and the value feeding spawn transforms
  agree bit-for-bit. [effort: M]

Deterministic source — pick during the grill:
- (a) **Discrete orientation table**: if island rotation is chosen from a small fixed candidate set
  (the placement comments describe candidate angles tried in fixed order), index a compile-time
  `constexpr (cos, sin)` table. Zero runtime trig, bit-identical everywhere. Simplest if the angle
  set is genuinely discrete. [effort: S]
- (b) **Deterministic `common::DeterministicSinCos(float radians, float& cos, float& sin)`** under
  `Common/Math/` (fixed-point / polynomial approx, no libm) if continuous angles are required.
  None exists today (verified: no `DeterministicCos`/`DeterministicSin` helper in the repo). Heavier;
  only justify if continuous rotation is a hard requirement. [effort: M]

Do NOT normalize the SAT axis in `ConvexHullsOverlap` — the boolean gap test does not need it and a
normalize would add a `sqrt` (determinism + perf cost) for no correctness gain.

## Critical files

- `Common/Math/ConvexHull.h` — remove `std::cos`/`std::sin` from `BuildWorldHull` (lines 23-24);
  change the signature (line 21) to take precomputed `fCos`/`fSin`.
- `Engine/Source/Frame/IslandChainPlacement.cpp` — update `BuildWorldHull` call sites (225, 271) to
  pass deterministic `(cos, sin)`; converge the file's own `std::cos`/`std::sin` uses (203-204,
  242-243, 246-247) onto the same deterministic source so all hull/spawn math is consistent.
- (If approach b) new `Common/Math/` deterministic sin/cos helper + its vcxproj/filters entries +
  `Common.h` aggregation include.

## Out of scope

- **SAT zero-length-axis "early-return no-overlap" (report H-2): DROPPED — not a bug here.** A
  degenerate/duplicate edge yields a zero axis, but then `fMinA == fMaxA == fMinB == fMaxB == 0`,
  so `fMaxA <= fMinB` is `0 <= 0` -> true -> the loop treats that axis as separating and would
  return `false`. That is a *false negative only when the zero axis is the loop's separating axis*;
  in practice `ExportIsland.cpp:170` already asserts convex + CCW at bake (no duplicate/collinear
  consecutive verts), so well-baked hulls never reach it. Per project policy ("assume parameters
  valid; do not add defensive validation"), do not add a zero-axis guard. If a future non-validated
  consumer appears, revisit. Not part of this plan.
- **Inverted-AABB empty-hull sentinel (report M-1): DROPPED** — pure defensive/validation ask;
  `iLocalCount`/`iVertexCount` are assumed valid. No change.
- **AABB `<` vs SAT `<=` strictness (report M-2): DROPPED** — both are exact, deterministic, and
  agree for current consumers; cosmetic/doc-only.
- **Perf items (report M-3 re-projection, M-4 modulo): DROPPED** — micro-optimizations bounded by
  small hull/chain sizes and the broadphase; not behavior-affecting. Not worth a plan now.
- **Style items (report M-5 int widths, L-1..L-6 naming/idiom/comments/IWYU): DROPPED** — cosmetic;
  route through a style pass, not this plan. (L-6 is explicitly style-compliant under the project's
  ExternalHeaders aggregation convention.)
- Input validation / defensive checks on hull counts or vertices — out by project policy.
- The broader DataPacker / `IslandChainPlacement` packing algorithm design.

## Acceptance criteria

- `BuildWorldHull` contains no `std::cos`/`std::sin`/`sinf`/`cosf`/`XMScalarSinCos` (no libm trig).
- World-hull vertices, `ConvexHullsOverlap` results, and resulting island placement are
  bit-identical across supported toolchains/platforms for the same inputs (deterministic CRC stable
  across rebuilds and across client/server).
- The `(cos, sin)` value that rotates the hull and the value that builds the island spawn transform
  in `IslandChainPlacement` come from the same deterministic source (no divergence between collision
  geometry and rendered/spawned geometry).

## Notes

- Source report this plan derives from: `Common Analysis/ConvexHull.md`. Only H-1 (transcendentals
  on the CRC path) survived validation; H-2, M-1..M-5, L-1..L-6 were dropped (see Out of scope).
- The report's claim that `BuildWorldHull`/`ConvexHullsOverlap` have real simulation/CRC-path
  consumers is correct and was independently re-verified (call sites + vcxproj + `Common.h`).
- Scope this together with the placement consumer's existing direct `std::cos`/`std::sin` calls;
  fixing only the header leaves an equivalent determinism hazard one frame away in the same path.
