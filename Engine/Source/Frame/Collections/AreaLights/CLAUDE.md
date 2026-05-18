# AreaLights - Quad-Based Area Lights

Client-only. Produces two GPU outputs per light: a ground-projected lighting deposit quad and a world-space visible-light quad.

## Unique Behaviors

- **Externally-supplied geometry**: owners `Sync` four visible corner positions every frame; no internal simulation, all PostRender sub-phases are no-ops. Types are registered externally (game/data), not by `Register()`.
- **Dual outputs in lockstep**: ground deposit and visible sprite share a single `siRendered` counter so both indirect draws stay index-aligned.
- **Orientation via corners**: visible quad uses raw corners; deposit quad is scaled around the visible quad's center so ground illumination inherits owner rotation without a separate rotation field.
- **Rectangular falloff**: per-axis smoothstep matches the quad footprint (not radial).
- **Minimum deposit size** = 8 * max lighting-deposit texel size (derived from current detail texture size and the camera's LOD-stable width/height) to prevent sub-texel flicker.
- **Intensity clamp invariant**: `fLightingIntensity * fIntensityMultiplier > 2.0` triggers `DEBUG_BREAK()`; callers must keep the product <= 2.0.
- **8-vertex AABB frustum cull** across all visible + lighting corners.

## See Also
- [../CLAUDE.md](../CLAUDE.md) - `Collection<T>`, SOA, Sync pattern, file-splitting conventions
