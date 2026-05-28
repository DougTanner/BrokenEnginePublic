# /Engine/Source/Frame/Collections/WindTrails/

Client-only directional wind-deposit quads, each oriented from a trail's previous render position to its current one. Sync pattern (parent owns lifetime).

## Unique Aspects

- Each trail builds one flat XY quad at base height spanning previous→current position; perpendicular for width is `cross(dir, worldZ)`. The previous→current vector is scaled by a per-trail length multiplier (shrinks/stretches the deposited streak), and trails with negligible motion or that fail visibility culling are skipped.
- Quads write into a shared dynamic `shaders::QuadLayout` buffer feeding two ping-pong wind-deposit pipelines (A/B). `EndRender` writes the indirect draw count only to the side matching the active wind texture; the other side gets 0.
- `Render()` carries an extra `uiFrameId` parameter so it is excluded from the auto-generated `InterpolateRenderTypes` walk.
- Whole pipeline early-outs when the wind setting is disabled; `ResetRenderState()` clears cached previous positions on world reset.

## See Also
- [../CLAUDE.md](../CLAUDE.md) - Collection framework, Sync pattern, render-only previous-position statics, GPU three-phase render
