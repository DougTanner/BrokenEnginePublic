# Engine/Data/Shaders/Wind - Wind Simulation Shaders

GPU-driven 2D wind field simulation using a hierarchical indirect compute dispatch architecture, sharing the smoke system's coordinate space.

## Overview

The wind system maintains a 2D velocity field (stored as RG channels) that advects over time. Objects deposit wind via quads in two modes: directional trail-shaped quads for moving objects and radial axis-aligned quads for explosions. The field simulates once per frame via compute shaders dispatched only over active tiles. The resulting wind velocity feeds back into the smoke simulation and affects visual elements like vegetation.

Wind shares smoke's dynamic world-area (`f4SmokeArea` / `f4PreviousSmokeArea`) and non-square aspect, so wind textures use independent X/Y tile counts.

## Architecture: Hierarchical Indirect Dispatch

Wind simulation uses two buffer pairs (A/B) matching the ping-pong texture index. Each pair consists of an occupancy buffer (bit-packed, one bit per 8x8 tile) and an active tile list buffer (indirect dispatch args + packed tile indices). This enables record-once command buffers: both spread pipelines (A and B) are always dispatched indirectly, but each checks `fWindTextureIndex` at runtime and returns early if inactive.

Each frame the active-tile pipeline (`WindOccupancyDilate.comp`) reads the previous frame's occupancy and writes the new active tile list. Because the world-area can shift and scale between frames, it first remaps each tile's center world position into the previous-frame tile grid, then dilates over a 5x5 box (radius 2) around that cell to catch wind that advected into neighbors. The spread shaders then process only the listed tiles and re-mark occupancy for next frame.

## Shaders

- **WindDeposit.frag** - Fragment shader writing wind velocity into the wind texture from per-object quads. Supports radial (explosions) and directional (motion trails) modes with falloff and magnitude scaling. Also writes to the occupancy buffer.
- **WindSpreadCommon.h** - Shared GLSL header containing the full `WindSpread()` function with advection, swirl, vorticity confinement, diffusion, and decay logic. Included by both spread compute shaders.
- **WindSpreadOne.comp** - Compute spread pass for ping-pong index 0 (writes TextureOne), 8x8 workgroups. Reconstructs world position from current `f4SmokeArea` and remaps to previous-frame texcoord via `f4PreviousSmokeArea` so sampling survives camera translation and zoom. A shared-memory reduction marks the output tile in occupancy once per workgroup when any texel produced non-zero wind. Returns early when index 1 is active.
- **WindSpreadTwo.comp** - Compute spread pass for ping-pong index 1 (writes TextureTwo). Identical to pass one but mirrored ping-pong index; unlike smoke, both wind passes import previous-frame state from the other texture, so both use the scale-aware lookup. Returns early when index 0 is active.
- **WindOccupancyDilate.comp** - Per-tile dilation + compaction into the active tile list for indirect dispatch (see Architecture above).

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory overview
