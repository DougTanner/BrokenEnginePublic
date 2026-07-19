# Engine/Data/Shaders/Smoke - Smoke Simulation Shaders

GPU-driven 2D smoke density field (single R channel) over a camera-following world rectangle. Objects deposit smoke via quads, two compute spread passes advect/diffuse/decay the field each frame, and the world passes (Water/Terrain/Model/Particles) sample the result through the smoke helpers in `ShaderFunctions.h`.

## Overview

Smoke owns the dynamic world-area (`f4SmokeArea` / `f4PreviousSmokeArea`, populated CPU-side from the camera visible area before Wind reads them). The field ping-pongs between two render-target textures using the same hierarchical indirect dispatch pattern as [Wind](../Wind/AGENTS.md): each texture has paired bit-packed occupancy, which is dilated and compacted into a shared active-tile list driving indirect dispatch. Spread cost therefore scales with smoke coverage rather than texture size; the texture and occupancy pairs start at zero on creation/recreate.

## Architecture: Recorded Frame Order

Pass B records before pass A; each frame runs dilate → B → dilate-remap → A → deposit → consumers:

1. **Pass B** (`SmokeSpreadTwo.comp`) reads TextureOne and its occupancy — the previous frame's pass A output plus deposits, still in previous-area coordinates — at the same UV (no remap), then writes TextureTwo and its occupancy.
2. **Pass A** (`SmokeSpreadOne.comp`) reads TextureTwo and its occupancy, performs the frame's single previous-area-to-current-area remap so smoke survives camera translation and zoom, then writes TextureOne and its occupancy.
3. **Deposit** (`Smoke.frag`, Main command buffer, paired with the Quads vertex shaders) adds fresh smoke into TextureOne and marks its occupancy before consumer passes, so consumers see same-frame deposits.

The one-sided remap forces two dilate variants, one before each spread:

- **SmokeOccupancyDilate.comp** (before B) - plain input-occupancy lookup at the output tile with 5x5 box dilation, unioned with the output texture's existing occupancy.
- **SmokeOccupancyDilateRemap.comp** (before A) - remaps each output tile's center into the previous-area tile grid before checking input occupancy, then unions the output texture's existing occupancy; without the remap, zoom drops smoke where displacement exceeds the dilation radius.

The output-occupancy union redispatches storage tiles written in an earlier frame even when camera remapping moves the smoke footprint elsewhere. Each half consumes that union before resetting and re-marking its output occupancy, so stale texels are rewritten and self-clear at exact zero without a per-frame image clear. The compute-shader header comments are authoritative for the lookup contract.

The enable/disable/recreate edges use indirect fullscreen clears in Main after Global spread: TextureTwo clears in its own render pass, then TextureOne clears at the start of the deposit render pass. Stale occupancy drains through the next spread frame.

## Shaders

- **Smoke.frag** - Deposits density from per-object quads with rotation, intensity falloff, and frame-rate-independent normalization (tuned against 60 Hz cadence). Clamps the falloff base to >= 0 before `pow` — interpolators can emit tiny negatives that would NaN.
- **SmokeSpreadCommon.h** - Shared spread logic with input texcoord, output world position, and wind texcoord decoupled as separate parameters (what lets the area shift and zoom between frames). Wind influence is two-mechanism: direct advection (moves smoke even in uniform wind fields) plus noise-modulated displacement (variation in gradient fields), blended against stationary smoke by a retention factor, then decayed. The rescaled wind X is negated — additive UV sampling reverses direction while Y cancels against the inverted texcoord Y; easy to "fix" incorrectly.
- **SmokeSpreadOne.comp / SmokeSpreadTwo.comp** - One workgroup per 8x8 tile from the active-tile list; a shared-memory reduction re-marks output-tile occupancy once per workgroup. The passes use distinct noise textures and scales. Pass B adds terrain-elevation decay (samples the shared elevation prepass), edge-of-area fade, and constant subtraction with threshold zeroing so faint smoke dies — load-bearing for the hierarchical culling, since lingering nonzero texels would keep tiles occupied forever.

## See Also

- `../AGENTS.md` - Shared includes and shader-wide conventions
- `../Wind/AGENTS.md` - Wind velocity field that drives smoke displacement; reads smoke's world-area
