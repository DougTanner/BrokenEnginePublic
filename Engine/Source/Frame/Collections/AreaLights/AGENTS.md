# AreaLights - Quad-Based Area Lights

Owner-driven quad lights producing two GPU outputs per light: a ground-projected lighting deposit quad (blurred texture variant) and a world-space visible-light quad (unblurred).

## Unique Behaviors

- Externally-supplied geometry: owners `Sync` four visible corner positions plus a per-light intensity multiplier every frame — the geometry itself is parent-supplied, not just a position; type index selects baked appearance.
- Dual outputs in lockstep: one frustum-cull decision (AABB over all 8 visible + lighting vertices) and a single shared render cursor keep the two indirect draws index-aligned and equal-count by construction.
- Orientation via corners: visible quad uses raw corners; deposit quad scales the corner offsets about the center (scale = `fLightingSize / corner-0 distance`) so ground illumination inherits owner rotation without a rotation field, then projects to base height via `ProjectToBaseHeight`.
- Minimum deposit size = 8 * max lighting-deposit texel size, preventing sub-texel flicker. `MinLightingDepositSize()` is authoritative: its texel basis is the un-bumped detail texture size, not the lighting-headroom-bumped one, because the headroom factor cancels in the shader's texel formula.
- Per-type render-time overrides: visible intensity, lighting size, and lighting intensity each read an optional `Wrapper*` on the type (live UI tuning) falling back to the baked value; the per-light multiplier scales both intensities but never the size.
