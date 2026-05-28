# Engine/Data/Shaders/Smoke - Smoke Simulation Shaders

Volumetric smoke simulation using ping-pong texture buffers with wind-driven displacement and hierarchical indirect dispatch.

## Overview

The smoke system maintains a single density field (R channel) that deposits, spreads, and decays over time. Objects deposit smoke trails, and the field propagates each frame via two alternating ping-pong spread passes (A then B) driven by **hierarchical indirect dispatch**: only tiles marked active in a bit-packed occupancy buffer are processed, eliminating GPU work on empty regions. The wind velocity field displaces smoke during spreading, with rescaled wind magnitude to control distortion.

The simulation shares wind's dynamic world-area (`f4SmokeArea` / `f4PreviousSmokeArea`) with non-square aspect, so smoke textures use independent X/Y tile counts.

## Architecture: Two Asymmetric Spread Passes

Pass A imports from the *previous* frame: at each output tile it reconstructs world position from current `f4SmokeArea`, then remaps to the previous-frame texcoord via `f4PreviousSmokeArea` so sampling survives camera translation and zoom. Pass B refines pass A's output *within the current frame* — input texcoord equals output texcoord, no cross-frame remap.

This asymmetry forces **two distinct occupancy-dilate passes**, one before each spread:

- **SmokeOccupancyDilateRemap.comp** runs before pass A. Because pass A samples a remapped (possibly zoomed) previous-frame location, the occupancy bit it must check lives at the remapped previous-area tile index, not the output tile. It computes that index per tile, then dilates by Manhattan distance 2. Without the remap, zoom drops smoke at edges where remap displacement exceeds the dilation radius.
- **SmokeOccupancyDilate.comp** runs before pass B. Pass B's sample is a plain same-tile lookup, so this is a direct occupancy check at the output tile with Manhattan-2 dilation.

Both compact the surviving active tile indices into the indirect dispatch buffer.

## Shaders

- **Smoke.frag** - Deposits smoke density from per-object quads with rotation, intensity falloff (`pow`), and frame-rate-independent normalization (tuned against 60 Hz cadence). Seeds the occupancy buffer for deposited tiles.
- **SmokeSpreadCommon.h** - Shared header for both spread passes. Wind influence uses two mechanisms: direct advection (shifts the base sampling coordinate in the wind direction for uniform fields) and noise-modulated displacement (scales displacement by wind noise amplitude for gradient fields). Adds swirl noise, then blends between wind-displaced and stationary smoke via a retention factor and applies a per-frame decay multiplier.
- **SmokeSpreadOne.comp** - Pass A spread. Reads the tile index from the active tile list and does the previous-frame scale-aware remap described above. Per-workgroup shared-memory reduction marks the output tile in occupancy once per workgroup.
- **SmokeSpreadTwo.comp** - Pass B spread (current-frame coords). Adds terrain-elevation-based decay, edge-of-area fade-out, and constant subtraction with threshold zeroing to eliminate lingering low-density smoke. Same shared-memory occupancy marking as pass A.

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory overview
- [../Wind/CLAUDE.md](../Wind/CLAUDE.md) - Wind velocity field that drives smoke displacement
