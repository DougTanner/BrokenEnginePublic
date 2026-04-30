# Particles - GPU-Driven Particle System Shaders

## Overview

Compute shaders for GPU-driven particle spawning and physics simulation, plus vertex/fragment shaders for rendering particles as velocity-stretched long quads, camera-facing square quads, and screen-space billboards. All particle state lives on the GPU with no CPU readback.

## Compute Pipeline

- **ParticlesSpawn.comp** - Single-threaded spawn shader that copies particles from the spawn buffer into free slots, computes delta time, and fills indirect dispatch/draw command buffers for update and render stages
- **ParticlesUpdate.comp** - Parallel update shader dispatched indirectly. Applies per-particle physics (integration, terrain collision, gravity, decay) and frees dead particles

## Render Shaders

- **LongParticlesRender.vert** - Velocity-oriented quads stretched along movement direction, billboarded toward camera
- **SquareParticlesRender.vert** - Camera-facing square quads with per-particle texture rotation
- **ParticlesRender.frag** - Shared fragment shader for long and square particles with intensity curve and smoke shadow attenuation
- **Billboards.vert/.frag** - Screen-space billboards with aspect-ratio-corrected sizing and alpha modulation

## Architecture Notes

- Spawn and update share the same particle storage buffer but use separate pipelines
- Indirect dispatch/draw buffers written by the spawn shader eliminate CPU-GPU synchronization for particle counts

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory overview (documents bindless texture conventions and descriptor set layout)
