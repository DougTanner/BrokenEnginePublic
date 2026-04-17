# /Engine/Source/Frame/Collections/Puffs/

Client-only fire-and-forget smoke puffs. All puffs are controlled (spawned via `AddControlled()`) and auto-destroy on animation expiry.

## Unique Aspects

- **Empty PostRender**: lifetime managed entirely through the interpolate collection; PostRender exposes no members and only `Destroy` has logic (delegates to `DestroyExpiredControlled`)
- **Custom controller type** with area/intensity/rotation keyframes. Per-keyframe multiplier arrays are re-applied every frame before interpolation (values live, not baked at spawn)
- **Selective copy**: `AllocateAndCopy` memcpys only controller bookkeeping; position and animatable fields recompute each frame from previous frame plus controller
- Render: axis-aligned smoke quads culled via `IsPointVisible` then projected to base height; quad params pack intensity (shader applies `pow(.y, fSmokeIntensityFalloff)`) and rotation

## See Also
- [../CLAUDE.md](../CLAUDE.md) - Collection framework, Controller pattern
