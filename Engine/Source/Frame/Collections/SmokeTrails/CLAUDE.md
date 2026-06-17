# /Engine/Source/Frame/Collections/SmokeTrails/

Client-only owner-driven smoke trail ribbons (Sync pattern): one quad per trail stretching from an exponentially smoothed tail position to the current head, deposited as density into the screen-space smoke simulation (`kDynamicPipelineSmoke`) rather than drawn as a textured sprite.

## Unique Aspects

- Two owners drive `Add`/`Sync`/`Remove` and register their own types: engine `Explosions` and game `Missiles`
- Smoothed positions are persistent sim-frame state (copied in `AllocateAndCopy`, not the render-only-state pattern) so the tail survives frame copies and reconciliation — the game's `ClientDataReceiver` patches them from the previous ring frame into incoming full states to keep rendering continuous. W == 0 flags uninitialized and snaps instantly; smoothing rate is a hard-coded constant, not a `gSmokeTrails*` tweak wrapper
- Geometry is a 2D ribbon on the base plane — both endpoints projected via `ProjectToBaseHeight`, width axis `cross(dir, +Z)`. Tail length scales with the per-frame segment length; per-corner width/length jitter comes from a local static unseeded `RandomEngine` (render-only, never sim state)
- Density normalized by segment length so deposited opacity is frame-rate and speed independent
- Post-spawn gate (~50 ms) forces length to 0 until a real movement delta exists; the id-reuse path (missile cross-cell transfer — the trail id rides in `TransferData`) zeroes the spawn time so the rebound trail renders immediately
- `Render()` carries an extra `uiFrameId` parameter solely so it is excluded from the auto-generated `InterpolateRenderTypes` walk (the same reason `WindTrails` carries it); the parameter itself is unused. Both are instead called directly from `RenderFrameMain()` with the per-frame ID

## See Also
- [../CLAUDE.md](../CLAUDE.md) - Collection framework, Sync pattern, three-phase render convention
