# /Engine/Source/Frame/Collections/WindTrails/

Directional wind-deposit quads: each trail emits one flat XY quad per rendered frame spanning its previous→current render position, carrying intensity and normalized motion direction that `WindDeposit.frag` injects as velocity into the ping-pong wind texture. Sync pattern (parent owns lifetime).

## Unique Aspects

- Quads build at base height; width axis is `cross(dir, worldZ)`; the previous→current vector scales by a per-trail length multiplier about the current position. Trails with negligible motion or failing visibility culling are skipped.
- After the build loop, every trail's current position — including culled and zero-motion trails — is snapshotted as next frame's previous position, so each quad spans exactly one render frame of motion and trails never smear when re-entering view; a trail deposits nothing its first rendered frame (previous defaults to current).
- Two ping-pong wind-deposit pipelines (A/B) share one dynamic `shaders::QuadLayout` buffer; `EndRender` writes the indirect draw count only to the side matching the active wind texture, the other gets 0 — both passes are always recorded, so the zero count is what no-ops the inactive side.
- `Render()` carries an extra `uiFrameId` parameter solely so it is excluded from the auto-generated `InterpolateRenderTypes` walk; like SmokeTrails (which shares the signature), the parameter itself is unused.
- Whole pipeline early-outs when the wind setting is disabled; `ResetRenderState()` clears cached previous positions on world reset.

## See Also
- `../AGENTS.md` - Collection framework, Sync pattern, render-only previous-position statics, GPU three-phase render
- `../WindRadials/AGENTS.md` - Stationary sibling; same deposit shader and ping-pong scheme
