# /Engine/Source/Frame/Collections/AreaLights/

Client-only quad-based area lights for projectiles and effects, using the Sync pattern for parent-provided positional data. Produces two GPU outputs per light: a lighting pass quad (ground-projected, for illumination) and a visible-light quad (world-space, for the sprite pass).

## File Structure

The implementation is split across three `.cpp` files:
- **AreaLights.cpp** - Registration, lifecycle (allocate/copy, spawn, transfer, destroy)
- **AreaLightsUpdate.cpp** - Update, sync, add/remove, collision phases
- **AreaLightsRender.cpp** - GPU resources and draw submission (`#ifdef BT_CLIENT` only)

## Architecture Notes

- `AreaLightsType` supports optional `Wrapper*` fields that override baked type values (intensity, area, lighting area) at render time, enabling live tweaking without modifying type registration
- Lighting quads preserve the orientation of the visible quad (scaled around the shared center), so the ground illumination matches the light's rotation
- The fragment shader uses rectangular falloff (smoothstep on both axes independently) rather than circular
- Frustum culling tests the AABB of all 8 vertices (4 visible + 4 lighting) via `AabbIntersectsVisibleArea`
- Lighting deposit quads enforce a minimum world-space size derived from the deposit texture resolution to prevent sub-texel flickering
- Deposit quads sample the pre-blurred texture (via `CrcToBlurredIndex()`); visible light sprites use the original unblurred texture (via `CrcToIndex()`)

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
