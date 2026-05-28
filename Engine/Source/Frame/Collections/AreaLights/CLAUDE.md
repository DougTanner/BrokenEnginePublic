# AreaLights - Quad-Based Area Lights

Client-only. Produces two GPU outputs per light: a ground-projected lighting deposit quad and a world-space visible-light quad.

## Unique Behaviors

- **Externally-supplied geometry**: owners `Sync` four visible corner positions plus a per-light intensity multiplier every frame; type index selects baked appearance. No internal simulation — types are registered externally (game/data), not by `Register()`.
- **Dual outputs in lockstep**: ground deposit and visible sprite share the single render cursor so both indirect draws stay index-aligned.
- **Orientation via corners**: visible quad uses raw corners; deposit quad is scaled around the visible quad's center (scale = `fLightingSize / corner-0 distance`) so ground illumination inherits owner rotation without a separate rotation field, then projected to base height via `ProjectToBaseHeight`.
- **Minimum deposit size** = 8 * max lighting-deposit texel size (derived from current detail texture size and the camera's visible-area width/height) to prevent sub-texel flicker.
- **Per-type render-time overrides**: visible intensity, lighting size, and lighting intensity each read an optional `Wrapper*` on the type (live UI value) falling back to the baked value; both intensities are then multiplied by the per-light multiplier.
- **8-vertex AABB frustum cull** across all visible + lighting corners.

## See Also
- [../CLAUDE.md](../CLAUDE.md) - `Collection<T>`, SOA, Sync pattern, file-splitting conventions
