# Particles - GPU-Driven Particle System Shaders

## Overview

Compute shaders for GPU-driven particle spawning and physics simulation, plus vertex/fragment shaders for rendering particles as velocity-stretched long quads, camera-facing square quads, and screen-space billboards. All particle state lives on the GPU with no CPU readback.

## Compute Pipeline

- **ParticlesSpawn.comp** - Single-threaded spawn shader that copies particles from the spawn buffer into free slots in the main particle buffer via sequential scan. Computes delta time from elapsed time and fills indirect dispatch/draw command buffers for the update, render, and lighting render stages. Supports full reset
- **ParticlesUpdate.comp** - Parallel update shader dispatched indirectly. Applies per-particle physics: position integration, terrain collision (bounces off elevation map), gravity, velocity decay, wind force from the ping-pong wind velocity field, intensity decay, size/length decay, and rotation. Frees particles that hit water, shrink below size epsilon, or fade below intensity epsilon

## Render Shaders

- **LongParticlesRender.vert** - Velocity-oriented quads stretched along the particle's movement direction, with length scaled by velocity magnitude between configurable start/end thresholds. Billboarded toward the camera via cross product of velocity direction and eye vector
- **SquareParticlesRender.vert** - Camera-facing square quads using world-up cross product for stable orientation. Supports per-particle texture coordinate rotation
- **ParticlesRender.frag** - Shared fragment shader for both long and square particles. Applies intensity with configurable power curve and packed RGBA color, with smoke shadow attenuation that fades based on height above the base elevation
- **Billboards.vert** - Screen-space billboards using pre-transformed clip-space positions with aspect-ratio-corrected sizing and texture coordinate rotation
- **Billboards.frag** - Fragment shader for billboards with per-billboard alpha modulation
- **LightingParticlesRender.vert/.frag** - Particle contribution to the deferred directional lighting pass, rendering into the visible area coordinate space and outputting uniform omnidirectional light across three MRT color channels (R/G/B)

## Architecture Notes

- Spawn and update share the same particle storage buffer but use separate pipelines
- Allocation tracking uses either byte-per-particle or 32-bit bitfield depending on a preprocessor define
- Indirect dispatch/draw buffers written by the spawn shader eliminate CPU-GPU synchronization for particle counts
- Wind sampling uses smoke area coordinates to match wind texture space

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory overview (documents bindless texture conventions and descriptor set layout)
