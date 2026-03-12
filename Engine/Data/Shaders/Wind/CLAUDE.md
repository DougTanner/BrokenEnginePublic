# Engine/Data/Shaders/Wind - Wind Simulation Shaders

GPU-driven 2D wind field simulation using a ping-pong texture architecture, sharing the smoke system's coordinate space.

## Overview

The wind system maintains a 2D velocity field (stored as RG channels) that advects over time. Objects deposit wind via quads in two modes: directional trail-shaped quads for moving objects and radial axis-aligned quads for explosions. The field simulates once per frame via a ping-pong render pass pair. The resulting wind velocity feeds back into the smoke simulation and affects visual elements like vegetation.

## Architecture: Ping-Pong

The wind system alternates which texture is the write target each frame (0/1). One texture is read as input while the other is written as output, then they swap next frame. Both render passes are always recorded in the command buffer (record-once pattern); the inactive pass is skipped via indirect draw buffer instance counts set to 0. Both textures preserve their contents when inactive (load-op load). Smoke shaders bind both wind textures and select the active one at runtime.

## Shaders

- **WindDeposit.frag** - Writes wind velocity into the wind texture from per-object quads. Supports radial (explosions) and directional (motion trails) deposit modes with falloff and magnitude scaling.
- **WindSpread.frag** - Full-screen simulation pass shared by both ping-pong pipelines. Combines advection, swirl perturbation, vorticity confinement, diffusion, and decay. All operations are time-scaled for framerate independence. Uses magnitude-dependent blending between Low/High parameter pairs so weak wind behaves differently from strong wind.

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory overview
