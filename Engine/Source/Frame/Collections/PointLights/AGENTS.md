# PointLights - Circular Point Lights

Client-only point lights support owner-driven and fire-and-forget controlled lifecycles. Each visible light emits a projected lighting deposit and a visible sprite from lockstep buffers.

## Invariants

- Culling must keep the deposit and sprite cursors aligned. The deposit projects to terrain or ocean base height; the sprite remains at the original world position.
- The visible sprite may be world-axis-aligned or camera-facing by type. The deposit is always world-axis-aligned and samples the blurred texture; the sprite samples the unblurred texture.
- Clamp deposit area to the renderer's minimum detail-texel footprint using the un-bumped detail texture size.
- Controller wrappers apply when controlled lights are seeded and animated. Owner `Sync` writes bypass those wrappers.
- `PersistentMembers()` carries type/controller/start-time metadata and base rotation through `AllocateAndCopyMembers()`; identity is owned and copied by the paired PostRender ID storage. Position and animated values are rewritten by owner sync or controller update, so any new persistent state must follow one of those lifecycles explicitly.
- Future controller start times remain at the first keyframe until animation begins.
