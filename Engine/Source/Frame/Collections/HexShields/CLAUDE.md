# HexShields - Geodesic Shield Meshes

Client-only geodesic shield meshes with directional damage visualization. Fully owner-driven — no autonomous simulation.

## Unique Aspects

- **Owner-driven lifecycle**: all `PostRender` phase hooks and `Transfer` are no-ops; owners drive everything via per-frame add/remove/sync.
- **Directional damage channels**: pointer-array cardinality matches the shader-side direction constant — keep header in sync. Independent vertex-stage and fragment-stage intensities per direction.
- **Asymmetric propagation**: owner-synced scalar fields memcpy forward; transforms and directional intensities propagate via previous-frame copy (no lerp) then get overwritten by sync. Last values persist on frames where sync has not yet run.
- **Dual pipeline, shared buffer**: main and lighting pipelines share one dynamic storage buffer; both rebind on resize and both receive the indirect-buffer write in `EndRender`.
- **Culled-packed GPU buffer**: per-element visibility test with margin; culled elements do not advance the write cursor, keeping the buffer tightly packed.
- **Packed ABGR type colors**: uint32 ABGR decoded to float4 inside the render loop.

## See Also
- [../CLAUDE.md](../CLAUDE.md) - Collection<T>, SOA, Sync pattern, file splitting conventions
