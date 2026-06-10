# Route hand-inlined visible-area mapping through the shared helper (DRY)

## Context

`ShaderFunctions.h` (`Engine/Data/Shaders/ShaderFunctions.h:21-33`) already exposes the canonical pair:

- `vec2 WorldToVisibleArea(vec3 f3WorldPosition, vec4 f4VisibleArea)` (`:21`) — world XY → `[0,1]` UV, including the `1 - mulY*(…)` Y-flip.
- `vec2 VisibleAreaToWorld(vec2 f2Uv, vec4 f4VisibleArea)` (`:29`) — the exact inverse (`:28` comment: "Inverse of WorldToVisibleArea (incl. its Y-flip)").

Eight shaders still hand-inline the *inverse* (`VisibleAreaToWorld`) math using the algebraically-identical verbose lerp form
`world = (1 - uv)*lo + uv*hi`, rather than calling the shared helper. The lighting instance in `LightingSpread.frag`
was just fixed during the Shadow Temporal-Accumulation follow-ups session — `LightingSpread.frag:74` now reads
`vec2 f2WorldPos = VisibleAreaToWorld(f2InTexcoord, globalLayout.f4LightingArea);`. This plan closes the remaining
sibling sites that were deliberately deferred from that session.

The form `(1 - uv.x)*lo + uv.x*hi` equals `lo + uv.x*(hi - lo)`, which is exactly the `VisibleAreaToWorld` X term.
The Y term is the subtlety: the helper applies `(1 - f2Uv.y)` against `(f4VisibleArea.y - f4VisibleArea.w)` anchored at
`.w`, i.e. it assumes the area is stored `{left, top, right, bottom}` with `top = .y > bottom = .w`. The inline sites
instead write `(1 - uv.y)*.y + uv.y*.w` (anchored at `.y`, lerping toward `.w`). These are equal **only because the
inline `(1-uv.y)*hiTop + uv.y*loBottom` collapses to the same world Y as the helper's `.w + (1-uv.y)*(.y - .w)`** —
each substitution must be re-derived per site before applying (the helper is *not* a blind drop-in for an arbitrary
`{x,y,z,w}` packing).

## Sites (verify each `path:line` + the exact area uniform and algebra at execution time)

All eight use the verbose lerp inverse form; the area uniform differs:

- `Engine/Data/Shaders/Terrain/Terrain.frag:55-60` — `main` reconstructs `f3InPosition` from `f2InVisibleAreaTexcoord`
  against `globalLayout.f4VisibleArea` (also samples elevation Z at `.z`, so only the XY pair is the candidate).
- `Engine/Data/Shaders/Water/Water.vert:39-43` — `f2WorldPosition` from `f2InTexcoord` against `globalLayout.f4VisibleArea`.
- `Engine/Data/Shaders/Water/WaterDisplacement.comp:44-48` — `f2WorldPosition` from `f2Texcoord` against `globalLayout.f4VisibleArea`.
- `Engine/Data/Shaders/Smoke/SmokeSpreadCommon.h:1-4` — the local helper `SmokeWorldPosition(vec4 f4SmokeArea, vec2 f2Texcoord)`
  is itself the inline inverse against `f4SmokeArea`.
- `Engine/Data/Shaders/Smoke/SmokeOccupancyDilateRemap.comp:49-51` — `f2WorldPosition` from `f2OutputUV` against `globalLayout.f4SmokeArea`.
- `Engine/Data/Shaders/Wind/WindSpreadOne.comp:55-57` — `f2WorldPosition` from `f2OutputTexcoord` against `globalLayout.f4SmokeArea`.
- `Engine/Data/Shaders/Wind/WindSpreadTwo.comp:56-58` — `f2WorldPosition` from `f2OutputTexcoord` against `globalLayout.f4SmokeArea`.
- `Engine/Data/Shaders/Wind/WindOccupancyDilate.comp:43-45` — `f2WorldPosition` from `f2OutputUV` against `globalLayout.f4SmokeArea`.

Smoke/Wind additionally carry a *forward* local helper `WorldToSmokeTexcoord(vec4 f4SmokeArea, vec2 f2Position)`
(`ShaderFunctions.h:202-206`) used at e.g. `WindSpreadOne.comp:59`. Note `WorldToSmokeTexcoord` is **not** identical to
`WorldToVisibleArea`: it has **no Y-flip** and divides by `(.w - .y)` (smoke area is packed `{left, top, right, bottom}`
with `top = .y`, `bottom = .w`, but its UV convention differs from the visible area's). The smoke/wind inverse inline
sites are paired with `WorldToSmokeTexcoord`, so their `f4SmokeArea` Y convention must be checked against the helper's
visible-area Y convention before any substitution — they may **not** be safe to route through `VisibleAreaToWorld`
without first proving the packing matches.

## Design

1. **Per-site inverse-derivation gate.** For each of the eight sites, expand both the inline expression and
   `VisibleAreaToWorld(uv, area)` symbolically and confirm byte-equivalent world XY (X term and the Y-flip term).
   Apply the substitution **only** where they match exactly. This is the same verification already done for the
   `LightingSpread.frag:74` site.
2. **`f4VisibleArea` sites (Terrain.frag, Water.vert, WaterDisplacement.comp):** these sample against the same
   `f4VisibleArea` the helper was written for, and the inline Y form matches the helper's. These are the clean
   substitutions — replace the inline block with `VisibleAreaToWorld(uv, globalLayout.f4VisibleArea)`.
3. **Smoke/Wind local-helper decision (the one design judgement):** decide between
   - (a) **Route through the shared helper.** Replace `SmokeWorldPosition` and the four raw `f4SmokeArea` inverse
     inlines with `VisibleAreaToWorld(uv, globalLayout.f4SmokeArea)` — **but only if** the smoke area's Y packing and
     UV convention are proven identical to the visible area's (verify against `WorldToSmokeTexcoord`'s non-flipped Y
     divide by `(.w - .y)`; if smoke UV is *not* Y-flipped the way `VisibleAreaToWorld` assumes, this changes results).
   - (b) **Leave the smoke/wind local helpers.** Keep `SmokeWorldPosition` (and `WorldToSmokeTexcoord`) as the
     smoke-family's own coordinate convention, and only fix the **raw** inline sites that duplicate `SmokeWorldPosition`'s
     body verbatim (collapsing the four raw `f4SmokeArea` inlines to a `SmokeWorldPosition(...)` call — a smaller DRY win
     entirely inside the smoke family, no cross-family convention coupling).

   Recommended default: **(b)** unless the Y-convention proof in step 1 is clean — the smoke/wind area Y packing is
   load-bearing for the spread-remap precision pact and is the higher-risk place to consolidate. Collapsing the raw
   inlines onto `SmokeWorldPosition` is the safe, in-family DRY win; promoting to `VisibleAreaToWorld` is only correct
   if the packings provably coincide.
4. **No behavior change.** Every applied substitution must be the *exact* algebraic inverse, so the compiled result is
   identical modulo float-reassociation (which `VisibleAreaToWorld`'s `lo + uv*(hi-lo)` form may differ from the
   inline `(1-uv)*lo + uv*hi` by — note this is a *different float rounding* than the inline form; for the snap-stable
   shadow/lighting areas this is sub-texel and below the smoke/wind decay floor, but call it out in the visual smoke-test).

## Out of scope

- The wind history-reset-on-recreate correctness item (`Graphics/WindHistoryResetOnRecreate.md`).
- The disabled-pass gating perf audit (`Graphics/DisabledPassGatingPerfAudit.md`).
- Any non-area-mapping shader cleanup (unrelated `normalize`/`pow` guards, descriptor-set audits, etc.).
- The lighting deposit/spread rework (`Graphics/LightingDepositSpreadFullPort.md`) — landed and was removed; it
  converted the lighting `LightingSpread.frag` site to the shared helper and left the eight non-lighting consumer
  sites to this plan, so the two never collided on the same lines.

## Acceptance criteria

- Each applied site calls `VisibleAreaToWorld(...)` (or `SmokeWorldPosition(...)` for in-family collapse) instead of
  open-coding the lerp; no remaining raw `(1 - uv)*area.x + uv*area.z` inverse inline at any of the eight sites that
  passed the equivalence gate.
- For any site that fails the equivalence gate (smoke/wind Y-convention mismatch), it is left untouched and the reason
  is recorded in the diff comment — not force-fitted.
- DataPacker shader recompile succeeds; the affected `data::kShaders*Crc` constants regenerate.
- Visual smoke-test (not compile-checked — these are runtime shaders): terrain color, water surface displacement,
  smoke plume drift, and wind-driven vegetation/smoke look pixel-unchanged under pan and zoom (the float-reassociation
  delta is sub-texel).

## Critical files

- `Engine/Data/Shaders/ShaderFunctions.h` (the `VisibleAreaToWorld` / `WorldToVisibleArea` / `WorldToSmokeTexcoord`
  helpers — read-only reference; not modified)
- `Engine/Data/Shaders/Terrain/Terrain.frag` (`main` world reconstruction)
- `Engine/Data/Shaders/Water/Water.vert` (`main` `f2WorldPosition`)
- `Engine/Data/Shaders/Water/WaterDisplacement.comp` (`main` `f2WorldPosition`)
- `Engine/Data/Shaders/Smoke/SmokeSpreadCommon.h` (`SmokeWorldPosition`)
- `Engine/Data/Shaders/Smoke/SmokeOccupancyDilateRemap.comp` (`main` remap)
- `Engine/Data/Shaders/Wind/WindSpreadOne.comp`, `Wind/WindSpreadTwo.comp`, `Wind/WindOccupancyDilate.comp` (`main` remap)

## Notes

- This is a pure DRY/consistency cleanup; the inline math is already correct, so the value is "one definition of the
  inverse, harder to drift" — not a bug fix.
- Requires a DataPacker shader recompile; there is no C++ compile that catches a wrong substitution, so the per-site
  equivalence gate (step 1) is the only safety net before the visual smoke-test.
- The lighting site fixed in the prior session is the worked example to copy verbatim.
