# Particles - GPU-Driven Particle System Shaders

## Overview

Compute shaders for GPU-driven particle spawning and physics simulation, plus vertex/fragment shaders for rendering particles as velocity-stretched long quads, camera-facing square quads, and screen-space billboards. All particle state lives on the GPU with no CPU readback.

## Compute Pipeline

- **ParticlesSpawn.comp** - Single-threaded (workgroup size 1) spawn shader. Copies particles from the spawn buffer into free slots in the main particle buffer using a sequential scan from `iMinFreeIndex`. Updates `iLastCount`, computes `fDeltaTime` from elapsed time, and fills indirect dispatch/draw command buffers for the update and render stages. Supports full reset via `iReset` flag
- **ParticlesUpdate.comp** - Parallel update shader (workgroup size 32) dispatched indirectly. Applies per-particle physics: position integration, terrain collision (bounces off elevation map), gravity, velocity decay, wind force from the 2D wind velocity field (ping-pong texture selected via `fWindTextureIndex`, scaled by `fParticlesWindStrength`), intensity decay, size/length decay, and rotation. Frees particles that hit water, shrink below size epsilon, or fade below intensity epsilon

## Render Shaders

- **LongParticlesRender.vert** - Velocity-oriented quads stretched along the particle's movement direction, with length scaled by velocity magnitude between configurable start/end thresholds. Billboarded toward the camera via cross product of velocity direction and eye vector
- **SquareParticlesRender.vert** - Camera-facing square quads using world-up cross product for stable orientation. Supports per-particle texture coordinate rotation
- **ParticlesRender.frag** - Shared fragment shader for both long and square particles. Samples from the global bindless `pTextures[]` array (Set 0 binding 4) using per-particle texture index with `nonuniformEXT()` dynamic indexing and a separate clamp sampler (Set 0 binding 12), applies intensity with configurable power curve and packed RGBA color. Applies smoke shadow attenuation via `SmokeShadow()` using the smoke texture (Set 1 binding 3) with height-based fade that reduces smoke influence at higher elevations
- **Billboards.vert** - Screen-space billboards using pre-transformed clip-space positions with aspect-ratio-corrected sizing and texture coordinate rotation
- **Billboards.frag** - Fragment shader for billboards, sampling from the global bindless `pTextures[]` array with `nonuniformEXT()` dynamic indexing using per-billboard `fTextureIndex`
- **LightingParticlesRender.vert/.frag** - Particle contribution to the deferred lighting pass, using the global bindless `pTextures[]` array for texture sampling

## Architecture Notes

- Spawn and update share the same `ParticlesLayout` storage buffer but use separate pipelines
- Particle and lighting particle shaders use the global Set 0 bindless `pTextures[]` array and clamp sampler for texture sampling, replacing per-pipeline combined sampler arrays
- Allocation tracking uses either byte-per-particle or 32-bit bitfield depending on `ENABLE_32_BIT_BOOL` define
- Indirect dispatch/draw buffers written by spawn shader eliminate CPU-GPU synchronization for particle counts
- Wind sampling uses `WorldToVisibleArea` with smoke area coordinates to match wind texture space
