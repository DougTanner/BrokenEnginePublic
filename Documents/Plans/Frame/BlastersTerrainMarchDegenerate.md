# BlastersUpdate Terrain-Impact March Is Degenerate (Endpoints Equal, Velocity Unused)

## Context

The terrain-intersection back-search in `BlastersUpdate` (`Projects/BrokenEngineSandbox/Source/Frame/
Collections/Blasters/BlastersUpdate.cpp:293-310`) is degenerate. When a blaster's post-integration position
falls at or below frame elevation, the code is meant to walk back along the blaster's travel segment to find
the *exact* terrain-intersection point so the impact effects spawn where the blaster actually hit the surface.
Instead, both endpoints of the search lerp are the **same** point, and the loaded velocity is **never used**:

```cpp
XMVECTOR vecVelocity = rCurrentPostRender.pVecVelocities[i];   // loaded, never read again
XMVECTOR vecInitialPosition = vecPosition;
XMVECTOR vecFinalPosition = vecPosition;                        // == vecInitialPosition

// "Binary search to find exact terrain intersection"  (comment also wrong — it is a linear march)
float fPercent = 0.0f;
XMVECTOR vecCollisionPosition = vecFinalPosition;
for (int64_t k = 0; k < kiTerrainSearchSteps; ++k, fPercent += kfTerrainSearchStepPercent)
{
    XMVECTOR vecPossibleCollisionPosition = XMVectorLerp(vecFinalPosition, vecInitialPosition, fPercent);
    // vecFinalPosition == vecInitialPosition, so this is just vecPosition for every k
    float fPossibleElevation = engine::gpIslandTerrain->FrameElevation(rStaticData, vecPossibleCollisionPosition);
    if (fPossibleElevation <= XMVectorGetZ(vecPossibleCollisionPosition)) { ...; break; }
}
```

Because `vecFinalPosition == vecInitialPosition == vecPosition`, `XMVectorLerp` returns `vecPosition` for every
`k`, the loop's first iteration's elevation test is the same condition that already triggered the impact
(`fPositionFinal <= fElevationFinal` at `:289`), and the loop effectively no-ops: it always resolves the
impact at the raw post-integration position (z snapped to the sampled elevation), plus the
`RandomPositionJitter` at `:313`. The intended "step back along the travel segment" never happens — the
**previous-tick (pre-integration) position is never reconstructed**, so the search has no second endpoint.

Two latent defects, both confirmed by inspection:

1. **Dead `vecVelocity` load** (`:293`) — read from the SOA and never used. Either it was meant to back out the
   pre-integration position (`vecPosition − vecVelocity·dt`) to form `vecInitialPosition`, or it is leftover.
2. **Degenerate march** (`:294-310`) — both endpoints equal, so the back-search does nothing; the impact point
   is the over-shot post-integration position, not the surface-crossing point. The misleading "Binary search"
   comment (`:297`) describes neither the structure (linear march) nor the current behavior (no search).

Visual-only consequence: terrain-impact craters/puffs/sounds spawn slightly past the true impact point
(by up to one tick of travel) and snapped down in z. **No determinism/CRC concern in the spawn position
itself** — but read Notes: the random draws here advance the shared engine and *are* in the shared path.

## Design

This is a **behavior question, not a pure mechanical fix** — confirm intent in the grill before implementing.

The crux: what was the search supposed to do, and does the fix touch the deterministic RNG order?

- **Reconstruct the travel segment.** Form the real back-search by setting `vecInitialPosition` to the
  *previous* position. The blaster integrates constant-velocity linear motion (per `Blasters/CLAUDE.md`:
  "Constant-velocity linear integration"), so the pre-integration point is `vecPosition − vecVelocity ·
  dt` (this is exactly what the dead `vecVelocity` load is for). Then `XMVectorLerp(vecPosition /*final*/,
  vecPrevPosition /*initial*/, fPercent)` walks back from the over-shot point toward the prior point and the
  loop finds the first sub-step at or above terrain — the intended exact-intersection march. Confirm `dt` is
  available at this site (the interpolate `fDeltaTime` / `kfTimeStep`).
- **Fix the comment** to "linear back-march" (it is not a binary search) and name the endpoints.

**Determinism caveat (grill decision):** the impact-effect block draws from `rFrame.postRender.randomEngine`
(`RandomPositionJitter` at `:313`, `Random<XM_2PI>` rotation at `:316`) — these advance the **shared** random
engine on *both* client and server (the `Blasters/CLAUDE.md` "Shared random-engine discipline" invariant).
The *number* and *order* of draws is what must stay identical across client/server, not the resulting position.
Moving the impact point (the `vecCollisionPosition` fed into `RandomPositionJitter`) changes the jittered
*output* but not the draw count/order, so client and server stay in lockstep **as long as the same code runs on
both sides** — which it does (the march is outside the `BT_CLIENT` guard; only the effect *spawns* at `:317+`
are client-only). Verify at execution that the march itself (and any `dt` it reads) is identical on both sides
and pulls no extra random draws. If the corrected march would ever early-`break` differently between sides,
that is a desync — but since the march is pure terrain sampling (no RNG) the risk is contained. State this
explicitly in the grill.

If the user decides the current spawn location is visually acceptable and the search was always vestigial, the
alternative is to **delete the dead march and the unused `vecVelocity` load** and spawn directly at the snapped
post-integration position (simplest; removes the dead code without reconstructing the segment). Present both.

## Out of scope

- The blaster's motion integration / collision detection (`:278-289`) — unchanged; this is only the
  post-impact effect-placement march.
- The `RandomPositionJitter` / rotation draws and the client-only effect spawns (`:312-320`) — their draw
  count/order must not change (determinism); only the *position* fed into the jitter moves.
- The smoke-puff / point-light / sound effect *types* and their keyframes — unchanged.
- Any change to `kiTerrainSearchSteps` / `kfTerrainSearchStepPercent` semantics beyond what the corrected
  endpoints require (if the segment is reconstructed, the existing step count/percent already sweep 0→1).
- Other collections' terrain-impact handling (Missiles area-damage, etc.) — out of scope.

## Acceptance criteria

- Terrain-impact effects spawn at (approximately) the true surface-crossing point along the blaster's last
  travel segment, not the over-shot post-integration position (if the reconstruct-segment option is chosen) —
  OR the dead march + unused `vecVelocity` load are removed and effects spawn at the snapped position (if the
  delete option is chosen).
- No change to the number or order of `rFrame.postRender.randomEngine` draws on either client or server (the
  march itself pulls no RNG; only the jitter input position changes).
- The "Binary search" comment is corrected (or removed with the dead code).
- Client and server build clean; a smoke test shows craters appearing on terrain hits as before.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/BlastersUpdate.cpp` — the impact block
  (`:285-320`); the degenerate march at `:294-310` and the dead `vecVelocity` load at `:293` are the change
  sites. `kiTerrainSearchSteps` / `kfTerrainSearchStepPercent` and `kfTerrainImpactJitter` are the relevant
  constants (defined near the top of the Blasters collection).
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/CLAUDE.md` — "Motion & collision" (constant
  -velocity integration → pre-position is `pos − vel·dt`) and "Shared random-engine discipline" (the
  determinism invariant the fix must preserve).

## Notes

- Latent visual bug, low blast radius; the effect just spawns a fraction of a tick past the real impact.
- The shared-CRC sim path is touched only insofar as the march runs on both sides — it pulls no RNG and the
  effect spawns are already client-gated, so the determinism risk is the *draw order*, which the fix must not
  perturb. Flag this for `/external-grill-plan`.
- One open decision: reconstruct the travel segment (fixes the search; uses the dead `vecVelocity`) vs delete
  the vestigial march (removes dead code; accepts the snapped post-integration spawn). Pre-staged above.
