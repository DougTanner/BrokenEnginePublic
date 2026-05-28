# Particles - GPU-Driven Particle System Shaders

## Overview

Compute shaders for GPU-driven particle spawning and physics simulation, plus vertex/fragment shaders for rendering particles as velocity-stretched long quads, camera-facing square quads, and screen-space billboards. All particle state lives on the GPU with no CPU readback.

## Compute Pipeline

- **ParticlesSpawn.comp** - Single-threaded spawn shader that copies particles from the spawn buffer into free slots, computes delta time, and fills indirect dispatch/draw command buffers for update and render stages
- **ParticlesUpdate.comp** - Parallel update shader dispatched indirectly. Applies per-particle physics (integration, terrain collision, gravity, decay) and frees dead particles

## Render Shaders

- **LongParticlesRender.vert** - Quads whose long axis follows the velocity direction; per-vertex length scaled by a velocity-driven stretch multiplier (`fParticlesStretchVelocity*` globals). Cross of to-eye and velocity-direction gives the width axis, with a singularity guard for view-parallel velocity (top-down camera) and an eye-facing fallback for zero-velocity / newly-spawned particles
- **SquareParticlesRender.vert** - Camera-facing square quads with per-particle texture rotation
- **ParticlesRender.frag** - Shared fragment shader for long and square particles. Intensity curve (`pow(intensity, intensityPower)`), bindless cookie texture, and smoke attenuation: a height-fraction fade above `fBaseHeight` is folded into the `SmokeShadow` sample. Designed for additive (`kAdd`) blend — output alpha is 0
- **Billboards.vert/.frag** - Screen-space billboards (NDC-positioned) with aspect-ratio-corrected sizing and per-particle texture rotation; fragment samples a bindless texture by index and scales by per-billboard alpha

## Architecture Notes

- Spawn and update share the same particle storage buffer but use separate pipelines
- Indirect dispatch/draw buffers written by the spawn shader eliminate CPU-GPU synchronization for particle counts. Spawn recomputes `iLastCount`, then sets the update dispatch to `ceil(count / kiParticleUpdateGroupSize)` workgroups and the indexed draw `instanceCount` to the particle count
- Allocation storage is `ENABLE_32_BIT_BOOL`-switched: a packed 32-bit-word bitmap (`puiAllocated`) or a `uint16_t` array (`pbAllocated`). Spawn claims free slots by scanning forward from `iMinFreeIndex`; update reclaims dead slots and `atomicMin`s `iMinFreeIndex` back down
- Each bitmap word is shared by 32 adjacent invocations — the parallel update shader must free slots with `atomicAnd`; only the spawn shader (`local_size_x = 1`) may write the bitmap non-atomically
- Update reads SSBO fields one at a time rather than copying the whole struct — glslang has been observed to drop trailing fields on scalar-block-layout SSBO struct copies (see `Terrain.vert`)
- Snap-to-terrain bounce: terrain-collision branch snaps z to `fTerrainElevation` and flips z-velocity via `abs()` rather than re-integrating a full dt step (avoids energy gain). Death test uses captured pre-collision z so the "kill on water hit" semantic survives the snap mutation
- Billboard cross-product NaN guard: ternary-perturb world-up when `|forward.z| > 0.999` so the world-up/forward cross is never zero (mirrors `Debug/DebugRenderBillboard.vert:36`)

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory overview (documents bindless texture conventions and descriptor set layout)
