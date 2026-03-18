# Engine/Data/Shaders/Wind - Wind Simulation Shaders

GPU-driven 2D wind field simulation using a hierarchical indirect compute dispatch architecture, sharing the smoke system's coordinate space.

## Overview

The wind system maintains a 2D velocity field (stored as RG channels) that advects over time. Objects deposit wind via quads in two modes: directional trail-shaped quads for moving objects and radial axis-aligned quads for explosions. The field simulates once per frame via compute shaders dispatched only over active tiles. The resulting wind velocity feeds back into the smoke simulation and affects visual elements like vegetation.

## Architecture: Hierarchical Indirect Dispatch

Wind simulation uses two buffer pairs (A/B) matching the ping-pong texture index. Each pair consists of an occupancy buffer (bit-packed, one bit per 8x8 tile) and an active tile list buffer (indirect dispatch args + packed tile indices). This enables record-once command buffers: both spread pipelines (A and B) are always dispatched indirectly, but each checks `fWindTextureIndex` at runtime and returns early if inactive.

Each frame the active-tile pipeline (`WindOccupancyDilate.comp`) reads the previous frame's occupancy, dilates by Manhattan distance 2 to include neighbors, and writes the new active tile list. The spread shaders then process only those tiles and write occupancy for the next frame.

## Shaders

- **WindDeposit.frag** - Fragment shader writing wind velocity into the wind texture from per-object quads. Supports radial (explosions) and directional (motion trails) modes with falloff and magnitude scaling. Also writes to the occupancy buffer.
- **WindSpreadCommon.h** - Shared GLSL header containing the full `WindSpread()` function with advection, swirl, vorticity confinement, diffusion, and decay logic. Included by both spread compute shaders.
- **WindSpreadOne.comp** - Compute spread pass for ping-pong index 0 (writes TextureOne). Returns early when index 1 is active.
- **WindSpreadTwo.comp** - Compute spread pass for ping-pong index 1 (writes TextureTwo). Returns early when index 0 is active.
- **WindOccupancyDilate.comp** - Dilates occupancy from previous frame and compacts into active tile list for indirect dispatch.

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory overview
