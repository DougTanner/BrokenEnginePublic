# HexShields - Geodesic Shield Meshes

Client-only geodesic shield meshes with directional damage visualization. Fully owner-driven — no autonomous simulation; the game-layer `Players` collection owns lifetime, per-frame `Sync` data, and damage-channel writes (including the slot-recycling policy).

## Unique Aspects

- Interpolate `Update` is not a stub: unlike a typical owner-driven collection (all PostRender hooks empty per the hub Sync pattern), the Interpolate-phase `Update` here carries transforms and directional intensities forward — see asymmetric propagation below.
- Directional damage channels: pointer-array count matches the shader-side direction constant — keep header in sync. Independent vertex-stage and fragment-stage intensities per direction.
- Asymmetric propagation: `PersistentMembers()` carries owner-driven scalar fields (position, type index, lighting/size/color-mix) through `AllocateAndCopyMembers()`; transforms and directional intensities instead carry forward via the Interpolate `Update`'s previous-frame copy (no lerp) then get overwritten by `Sync`. Last values persist on frames where sync has not yet run.
- Dual pipeline, shared buffer: main and lighting pipelines share one dynamic storage buffer; both rebind on resize and both receive the indirect-buffer write in `EndRender`.
- Culled-packed GPU buffer: per-element visibility test with margin; culled elements do not advance the write cursor, keeping the buffer tightly packed.
- Type-registry colors: `TypeRegistry<HexShieldsType>` holds the per-type-index main + lighting colors (packed ABGR uint32) and minimum intensity; both colors decode to float4 inside the render loop.
