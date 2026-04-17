# /Engine/Source/Frame/Collections/SmokeTrails/

Client-only externally-managed smoke trails with frame-rate-independent exponential position smoothing.

## Unique Aspects

- Smoothed positions persist across frames (not render-only state), copied during `AllocateAndCopy`. W == 0 flags uninitialized and snaps instantly
- Geometry is a 2D ribbon on the base plane — both endpoints projected via `ProjectToBaseHeight`, width axis is `cross(dir, +Z)`
- Density normalized by segment length so opacity is frame-rate and speed independent
- Short post-spawn gate (~50 ms) forces length to 0 until a real delta exists; the reuse path resets the spawn time so the reused slot renders immediately
- Lifetime is strictly owner-driven; all PostRender phase hooks are no-ops (no controller / auto-expire)
- `Render()` takes an extra `uiFrameId` and is invoked separately from the standard `InterpolateRenderTypes` fan-out

## See Also
- [../CLAUDE.md](../CLAUDE.md) - Collection framework, Sync pattern, file-splitting convention
