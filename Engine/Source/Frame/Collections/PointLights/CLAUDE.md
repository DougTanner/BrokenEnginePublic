# PointLights - Circular Point Lights

Client-only circular point lights with synced (parent-owned) and controlled (fire-and-forget) lifecycles. Each active light emits two GPU quads per frame: a base-height-projected lighting deposit and a visible-light sprite.

## Unique Aspects

- **Dual pipeline ownership**: owns both the axis-aligned lighting deposit pipeline and the visible-lights sprite pipeline. Two dynamic buffers sized in lockstep from a single capacity-accumulation pass.
- **Base-height projection**: deposit position projects onto the ocean plane; visible sprite keeps original world Y. Both quads gated by point-visibility culling.
- **Camera-aligned toggle**: a Type-level flag flips the visible sprite between world-axis-aligned (ground effects) and camera-facing billboard (volumetric sources). Deposit quad is always world-axis-aligned.
- **Texture sampling split**: deposit uses the blurred texture index; visible sprite uses the unblurred index.
- **Minimum lighting area clamp**: area floored against detail-texel size scaled by the deposit multiplier to prevent sub-texel flicker.

## Controller vs. Sync Divergence

Per-keyframe `Wrapper*` scales (visible/lighting area and intensity) apply only on the controlled path — at add-time seeding and during per-frame interpolation. Synced writes bypass them. Type-level wrappers exist for external default tuning but are not read here; there is no visible-area wrapper at the Type level.

## Phase Usage

Only `Destroy` is active (removes expired controlled lights). `Transfer` is a no-op — the owning entity handles transfer. `Register()` is empty; types are registered externally.
