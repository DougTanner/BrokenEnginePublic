# /Engine/Source/Frame/Collections/WindTrails/

Client-only directional wind-simulation quads rendered from previous-to-current position.

## Unique Aspects

- Previous-position history lives in file-scope statics keyed by trail ID (render-only, out of dual-buffered frame data)
- Each trail renders an oriented quad from base-height-projected previous position to current; perpendicular is `cross(dir, worldZ)` so quads lie flat in XY at base height
- A/B ping-pong wind-deposit pipelines share one dynamic quad buffer; indirect draw count written only to the side matching the active wind texture, the other receives 0
- `Render()` takes an extra `uiFrameId` parameter (excluded from `InterpolateRenderTypes`, invoked separately in the render pipeline)
- Early-outs when wind setting disabled

## See Also
- [../CLAUDE.md](../CLAUDE.md) - Collection framework, Sync/render-state patterns
