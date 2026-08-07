<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-07T00:01:02.224Z","dependsOn":[]} -->
# Remove the now-redundant outward-zoom lighting temporal reset

## Context

`Projects/BrokenEngineSandbox/Source/Graphics/Camera.cpp:~281-286` forces a full lighting temporal reset on every frame with outward eye movement:

```
// Lighting temporal history has no prior-valid-window bounds, so every frame with actual outward eye movement
// uses pure-current lighting and re-seeds history after the enlarged world area is rendered. ...
if (mfCameraEyeHeight > fEyeHeightBeforeZoom)
{
	engine::gbLightingTemporalReset = true;
}
```

The stated justification is no longer true. The lighting/shadow dispatch windowing removal landed in this session gave `Engine/Data/Shaders/Lighting/LightingTemporal.comp` the same per-texel history rejection that `Shadow/ShadowTemporal.comp` already had: the texel's world position is reprojected into the previous frame's visible area, and history is discarded when the reprojected UV falls outside `[0,1]` (`LightingTemporal.comp:42-45`). That per-texel rejection is exactly the mechanism that lets the shadow path avoid a global reset on outward zoom, so the lighting global reset now appears to duplicate it — at the cost of throwing away all valid history every zoom-out frame.

This is a consequence of the session's change rather than a defect it introduced, and evaluating it needs live visual verification, so it was recorded here instead of widening that change.

## Design

Delete the outward-zoom reset and its stale justification comment, letting `LightingTemporal.comp`'s reprojection rejection handle newly revealed area exactly as it already does for shadows. `gbLightingTemporalReset` itself stays: other setters (initialization, device loss, and any other discontinuity) still need it.

Prove the removal with live verification before accepting it: lighting must stay continuous while zooming out, with no visible smearing or stale-lighting trail from history that should have been rejected, and no darkening or popping band at the newly revealed edges. If verification shows a real artifact, the removal is not correct as written — stop and surface it for re-planning rather than substituting a partial reset heuristic.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Graphics/Camera.cpp` — the outward-zoom block near line 281 inside the eye-height update
- `Engine/Data/Shaders/Lighting/LightingTemporal.comp` — read-only reference for the rejection that replaces the reset

## In scope

- The `if (mfCameraEyeHeight > fEyeHeightBeforeZoom)` reset block in `Camera.cpp` and its preceding comment
- `fEyeHeightBeforeZoom` and any other local that becomes unused once the block is gone
- Any AGENTS.md sentence that documents the outward-zoom lighting reset

## Out of scope

- `gbLightingTemporalReset` itself and every other site that sets it
- `LightingTemporal.comp`, `ShadowTemporal.comp`, and the temporal blend weights
- `UpdateTexelEyeHeightReference`, the shadow and lighting texel-ramp references, and the Hermite eye-height blend
- Inward zoom behavior

## Risk tier and invariants

Expected Change Workflow Tier 2 — client-only visual behavior in one subsystem. Temporal lighting is interpolate/render-side and client-only, so it is outside the deterministic PostRender state and the per-tick CRC; no serialization, wire, or threading surface is touched.

## Acceptance criteria

- Live verification through `/agent-harness`: zooming the camera out shows continuous lighting with no smearing, stale-lighting trail, or dark/popping band at newly revealed screen area, compared against the same motion before the change
- Client compiles and the comment describing lighting temporal history no longer claims it has no prior-valid-window bounds
