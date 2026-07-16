# Engine/Data/Shaders/Smoke - Smoke Simulation Shaders

GPU-driven 2D smoke density field (single R channel) over a camera-following world rectangle. Objects deposit smoke via quads, two compute spread passes advect/diffuse/decay the field each frame, and the world passes (Water/Terrain/Model/Particles) sample the result through the smoke helpers in `ShaderFunctions.h`.

## Overview

Smoke owns the dynamic world-area (`f4SmokeArea` / `f4PreviousSmokeArea`, populated CPU-side from the camera visible area before Wind reads them). The field ping-pongs between two render-target textures using the same hierarchical indirect dispatch pattern as [Wind](../Wind/AGENTS.md): a bit-packed occupancy buffer (one bit per 8x8 tile) is dilated and compacted into an active-tile list driving `vkCmdDispatchIndirect`, so spread cost scales with smoke coverage rather than texture size — empty ocean costs nothing.

## Architecture: Recorded Frame Order

Pass B records before pass A; each frame runs dilate → B → dilate-remap → A → deposit → consumers:

1. **Pass B** (`SmokeSpreadTwo.comp`, pipeline SmokeSpreadComputeB) reads TextureOne — the previous frame's pass A output plus deposits, still in previous-area coordinates — at the same UV (no remap) and writes TextureTwo.
2. **Pass A** (`SmokeSpreadOne.comp`, pipeline SmokeSpreadComputeA) reads TextureTwo and performs the frame's single coordinate remap (previous area → current area), so the field survives camera translation and zoom; writes TextureOne.
3. **Deposit** (`Smoke.frag`, Main command buffer, paired with the Quads vertex shaders) adds fresh smoke into TextureOne and seeds occupancy before the consumer passes, so consumers see same-frame deposits.

The one-sided remap forces two dilate variants, one before each spread:

- **SmokeOccupancyDilate.comp** (before B) - plain occupancy lookup at the output tile, 5x5 box dilation.
- **SmokeOccupancyDilateRemap.comp** (before A) - remaps each output tile's center into the previous-area tile grid and checks occupancy there; without the remap, zoom drops smoke at edges where the displacement exceeds the dilation radius.

Both compact surviving tile indices into the indirect dispatch buffer. The .comp header comments are the authoritative statement of this contract.

## Shaders

- **Smoke.frag** - Deposits density from per-object quads with rotation, intensity falloff, and frame-rate-independent normalization (tuned against 60 Hz cadence). Clamps the falloff base to >= 0 before `pow` — interpolators can emit tiny negatives that would NaN.
- **SmokeSpreadCommon.h** - Shared spread logic with input texcoord, output world position, and wind texcoord decoupled as separate parameters (what lets the area shift and zoom between frames). Wind influence is two-mechanism: direct advection (moves smoke even in uniform wind fields) plus noise-modulated displacement (variation in gradient fields), blended against stationary smoke by a retention factor, then decayed. The rescaled wind X is negated — additive UV sampling reverses direction while Y cancels against the inverted texcoord Y; easy to "fix" incorrectly.
- **SmokeSpreadOne.comp / SmokeSpreadTwo.comp** - One workgroup per 8x8 tile from the active-tile list; a shared-memory reduction re-marks output-tile occupancy once per workgroup. The passes use distinct noise textures and scales. Pass B adds terrain-elevation decay (samples the shared elevation prepass), edge-of-area fade, and constant subtraction with threshold zeroing so faint smoke dies — load-bearing for the hierarchical culling, since lingering nonzero texels would keep tiles occupied forever.

## See Also

- `../AGENTS.md` - Shared includes and shader-wide conventions
- `../Wind/AGENTS.md` - Wind velocity field that drives smoke displacement; reads smoke's world-area
