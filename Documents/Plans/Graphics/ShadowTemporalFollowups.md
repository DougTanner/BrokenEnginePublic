# Follow-ups from the Shadow Temporal-Accumulation change

## Context

A temporal-accumulation pass (`ShadowTemporal.comp` / `kPipelineShadowTemporal`) was added after `ShadowBlurV`
to de-flicker the Phase-2 shadow texel ramp: it reprojects the previous frame's blurred shadow
(`mShadowHistoryTexture`, via `f4ShadowAreaPrevious`) into the current grid and EMA-blends it in place into
`mShadowBlurTexture`, then `vkCmdCopyImage`-refreshes the history. The final audits passed (SOUND); these items
were deliberately deferred as out-of-scope and **none affect correctness of the shipped change**.

## 1. Temporal pass + history copy run every frame even when disabled (perf)

When `gShadowTemporalBlend == 1.0` (the slider's "disabled" value; default is 0.2), the temporal dispatch, the
**full-texture** `vkCmdCopyImage`, and four layout transitions still execute every frame on the record-once
command buffer. `mix(history, current, 1.0)` is a no-op blend, but the copy still blits the whole (pre-sized
~4×-of-visible) R16 texture.

- The copy is full-texture because its region is fixed at record time and a fast zoom-out transient can grow the
  visible window to the full texture — so it **cannot** be safely shrunk to the window without re-record.
- Gating the whole pass+copy when disabled would need either a destroy-tier re-record on the enable/disable edge
  (today a blend change flows purely through the uniform — no re-record) or an indirect/conditional dispatch
  (and the copy still can't be indirect-gated).
- **Action**: measure the copy+pass cost via `kGpuTimerShadow` at representative resolutions; if non-trivial,
  evaluate gating-when-disabled (re-record on edge) vs. accepting the fixed cost. Low priority — likely small.

## 2. Device-lost / settings recreate seeds stale history for one frame

The first-frame guard (`sbPreviousShadowAreaInitialized` / `sf4PreviousShadowArea`, function-local statics in
`GlobalUniforms.cpp::PopulateShadowParameters`) forces `fShadowTemporalBlend = 1.0` on the very first frame so the
uninitialized `mShadowHistoryTexture` is never blended. But those statics **survive** a `DeviceLostException`
Graphics recreate (and a resolution/settings recreate), while `mShadowHistoryTexture` is recreated with undefined
contents (`textureFlags = {}`, no clear). Result: for one frame after a recreate the blend uses the live weight
(e.g. 0.2) against garbage history — a single-frame transient on a rare path. (The smoke/wind previous-area latch
shares this same edge class.)

- **Action**: reset `sbPreviousShadowAreaInitialized = false` when the shadow textures are recreated (e.g. from
  `CreateShadowTextures`, guarding the static via a small reset hook), **or** clear `mShadowHistoryTexture` on
  create. Prefer the reset hook (cheaper than a clear). Minor.

## 3. (DRY) `LightingSpread.frag` inlines the same area-inverse now in `VisibleAreaToWorld`

This change added `VisibleAreaToWorld` to `ShaderFunctions.h` (the exact inverse of `WorldToVisibleArea`).
`Engine/Data/Shaders/Lighting/LightingSpread.frag` (~:74-75) already inlines the identical inverse for
`f4LightingArea`. Pre-existing duplication, unrelated to this change — route it through the shared helper in a
future DRY pass. Trivial.

## Notes
- All three are graphics/client-only; no determinism, CRC, or server impact.
- Item 1 is the only one with a perf angle worth measuring; 2 and 3 are small/rare and can ride along.
