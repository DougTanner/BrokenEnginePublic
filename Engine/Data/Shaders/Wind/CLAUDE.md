# Engine/Data/Shaders/Wind - Wind Simulation Shaders

GPU-driven 2D wind field simulation using a ping-pong texture architecture, sharing the smoke system's coordinate space.

## Overview

The wind system maintains a 2D velocity field (stored as RG channels) that advects over time. Objects deposit wind via quads in two modes: directional trail-shaped quads for moving objects and radial axis-aligned quads for explosions. The field simulates once per frame via a ping-pong render pass pair. The resulting wind velocity feeds back into the smoke simulation and affects visual elements like vegetation.

## Architecture: Ping-Pong

The wind system alternates which texture is the write target each frame via `giWindTextureIndex` (0↔1):

- **Even frames**: WindSpread writes TextureOne (reading TextureTwo), WindDeposit writes TextureOne
- **Odd frames**: WindSpreadTwo writes TextureTwo (reading TextureOne), WindDepositTwo writes TextureTwo

Both textures are render targets with `VK_ATTACHMENT_LOAD_OP_LOAD` to preserve contents when inactive. Both render passes are always recorded in the command buffer (record-once pattern); conditional behavior is controlled by indirect draw buffer instance counts, with the active pass getting instance count 1 and the inactive pass getting 0. Smoke shaders bind both wind textures and select the active one via `fWindTextureIndex` uniform.

## Shaders

- **WindDeposit.frag** - Writes wind velocity into the wind texture from per-object quads. Supports two modes selected by per-vertex `f4InParams.w`: **radial mode** (w > 0.5) computes outward direction from quad center using texcoord offset, producing expanding circular wind for explosions; **directional mode** (w <= 0.5) uses CPU-computed wind direction from `f4InParams.yz` for motion-trail deposits. Both modes sample a falloff texture and scale by magnitude from `f4InParams.x`.
- **WindSpread.frag** - Full simulation pass reading from the opposite texture and writing to the active target (used by both WindSpread and WindSpreadTwo pipelines). All operations are time-scaled via `fWindTimeScale` (delta-time * 60 * time scale) for framerate independence. Performs an early-out by reading the center and 4 cardinal neighbors upfront and checking total energy (sum of squared magnitudes); pixels where all five samples are zero skip all computation. Computes a magnitude factor (`fMagFactor`) by linearly remapping magnitude through `fWindThresholdLow`/`fWindThresholdHigh` with clamping. All propagation parameters use High/Low pairs mixed by `fMagFactor` via `mix()`, enabling magnitude-dependent behavior where weak and strong wind behave differently. Slider semantics are intuitive (up = more effect): decay sliders control energy dissipation (high value = faster decay, applied as `1.0 - decayValue` internally), and momentum sliders control directional inertia (high value = more momentum = less lateral spread/swirl/diffusion, applied as `1.0 - momentumValue` to derive a spread factor). Semi-Lagrangian advection traces back along wind direction scaled by the spread factor so high-momentum wind advects directionally while low-momentum wind spreads laterally. Noise-driven swirl perturbation uses animated noise sampling (world-space UV scaled by swirl scale and offset by `fWindTime * swirlSpeed`) for time-varying turbulence patterns. The 4 neighbor reads are shared between the early-out, vorticity confinement, and diffusion. Vorticity confinement mode is selected via preprocessor defines (`VORTICITY_NONE`, `VORTICITY_SIMPLE`); currently `VORTICITY_SIMPLE` is active, applying a perpendicular force proportional to omega along the wind direction. 4-neighbor diffusion blending also scales with the spread factor. After exponential decay, a tanh soft clamp limits high magnitudes, then a constant decay subtracts a small fixed amount so near-zero values converge cleanly to exactly zero rather than lingering indefinitely.

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory overview
