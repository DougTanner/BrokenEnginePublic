# PointLights - Circular Point Lights

Client-only circular point lights with synced (parent-owned) and controlled (fire-and-forget) lifecycles. Each visible light emits two GPU quads per frame: a base-height-projected lighting deposit and a visible-light sprite.

## Unique Aspects

- **Dual pipeline ownership**: owns both the axis-aligned lighting deposit pipeline and the visible-lights sprite pipeline. Two dynamic buffers sized in lockstep from a single capacity-accumulation pass; a single shared render cursor keeps them index-aligned, so a culled light skips both quads.
- **Base-height projection**: deposit position projects to base height via `ProjectToBaseHeight` (terrain elevation, ocean at minimum); the visible sprite keeps the original world position.
- **Camera-aligned toggle**: a Type-level flag flips the visible sprite between world-axis-aligned (ground effects) and camera-facing billboard (volumetric sources). Deposit quad is always world-axis-aligned.
- **Texture sampling split**: deposit uses the blurred texture index; visible sprite uses the unblurred index.
- **Minimum lighting area clamp**: lighting area floored to 8 detail texels to prevent sub-texel flicker. Texel basis is the un-bumped `DetailTextureSize`, not `LightingDetailTextureSize` — the lighting-headroom pre-size cancels in the deposit texel formula (see the comment in `PointLightsRender.cpp`).
- **Selective copy**: `AllocateAndCopy` memcpys only identity and controller bookkeeping (type index, controller index, start time, base rotation); position and the animatable area/intensity/rotation values are rewritten every frame by Interpolate `Update` or the owner's `Sync` — a new persistent member must join the copy list or it silently resets.

## Controller vs. Sync Divergence

Per-keyframe `Wrapper*` scales (visible/lighting area and intensity) apply only on the controlled path — at add-time seeding and during per-frame interpolation. Synced writes bypass them. Type-level wrappers exist for external default tuning but are not read here; there is no visible-area wrapper at the Type level.

## Phase Usage

Interpolate `Update` runs per-frame controller keyframe animation (wrapper-scaled) for controlled lights; synced lights are written each frame by their owner via `IdToIndex`. Controlled lights may be added with a future start time (keyframe interpolation clamps to keyframe 0 until then) — Explosions schedules delayed secondary lights this way. Among PostRender phases only `Destroy` has logic (removes expired controlled lights).

## See Also

- [../AGENTS.md](../AGENTS.md) - `Collection<T>`, SOA layout, Sync and controller patterns, GPU render conventions
