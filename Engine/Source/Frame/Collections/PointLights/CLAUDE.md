# /Engine/Source/Frame/Collections/PointLights/

Client-only circular point lights with optional keyframe animation, using the Sync and Controller patterns. Produces two GPU outputs per light: an axis-aligned lighting quad (base-height projected) and a visible-light sprite quad (world-space).

The visible-light quad has two modes controlled by `PointLightsType::bCameraAligned`:
- **`false` (default)**: World-space axis-aligned quad — corners offset along world X/Y axes. Suitable for ground-plane effects like fire or pools of light.
- **`true`**: Camera-aligned billboard — corners computed from the camera's right and up vectors so the quad always faces the viewer. Suitable for volumetric or floating light sources.

## File Structure

The implementation is split across three `.cpp` files:
- **PointLights.cpp** - Registration, lifecycle (allocate/copy, spawn, transfer, destroy with auto-expiry of controlled lights)
- **PointLightsUpdate.cpp** - Update, sync, add/remove/addControlled, collision phases
- **PointLightsRender.cpp** - GPU resources and draw submission (`#ifdef BT_CLIENT` only)

## Architecture Notes

- Texture lookup in `Render()` uses `gpTextureManager->mTextureDescriptors.CrcToIndex(rType.crc)` per type to resolve the GPU texture slot
- Supports both synced lights (parent-managed) and controlled lights (fire-and-forget with keyframe animation that auto-destroys on expiry)
- Controlled lights use `InterpolateKeyframes()` each frame to drive visible area, intensity, lighting area, and rotation
- Lighting deposit quads enforce a minimum world-space size derived from the deposit texture resolution to prevent sub-texel flickering

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
