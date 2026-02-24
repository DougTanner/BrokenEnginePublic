# Engine/Data/Shaders/Wind - Wind Simulation Shaders

GPU-driven 2D wind field simulation using a ping-pong texture architecture, sharing the smoke system's coordinate space.

## Overview

The wind system maintains a 2D velocity field (stored as RG channels) that advects over time. Objects deposit wind via quads in two modes: directional trail-shaped quads for moving objects and radial axis-aligned quads for explosions. The field simulates once per frame via a ping-pong render pass pair. The resulting wind velocity feeds back into the smoke simulation and affects visual elements like vegetation.

## Architecture: Ping-Pong

The wind system alternates which texture is the write target each frame (0/1). One texture is read as input while the other is written as output, then they swap next frame. Both render passes are always recorded in the command buffer (record-once pattern); the inactive pass is skipped via indirect draw buffer instance counts set to 0. Both textures preserve their contents when inactive (load-op load). Smoke shaders bind both wind textures and select the active one at runtime.

## Shaders

- **WindDeposit.frag** - Writes wind velocity into the wind texture from per-object quads. Supports two deposit modes: **radial** for explosions (outward direction computed from quad center) and **directional** for motion trails (CPU-computed wind direction). Both modes apply a falloff texture and magnitude scaling.
- **WindSpread.frag** - Full-screen simulation pass shared by both ping-pong pipelines. All operations are time-scaled for framerate independence. Early-outs when center and 4 cardinal neighbors are all zero (most of the texture). Uses magnitude-dependent behavior where all propagation parameters blend between Low/High pairs based on wind strength, so weak wind behaves differently from strong wind. Key simulation stages: semi-Lagrangian advection (trace-back along wind direction), noise-driven swirl perturbation, optional vorticity confinement (preprocessor-selected, currently simple mode), 4-neighbor diffusion, exponential decay with tanh soft clamping, and constant decay to converge near-zero values cleanly to zero. Momentum controls the balance between directional advection and lateral spread/swirl/diffusion.

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory overview
