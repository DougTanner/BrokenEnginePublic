# Engine/Data/Shaders/Smoke - Smoke Simulation Shaders

Volumetric smoke simulation using ping-pong texture buffers with wind-driven displacement and hierarchical indirect dispatch.

## Overview

The smoke system maintains a density field that deposits, spreads, and decays over time. Objects deposit smoke trails, and the field propagates each frame via two alternating spread passes driven by **hierarchical indirect dispatch**: only tiles marked active in a bit-packed occupancy buffer are processed, eliminating GPU work on empty regions. The wind velocity field displaces smoke during spreading, with clamped displacement to prevent excessive distortion.

The simulation world-area is dynamic — derived each frame from the camera's render visible-area scaled by a margin multiplier, with non-square aspect inherited from the framebuffer. Spread passes survive both translation and scale changes by remapping the previous frame's UV via `f4PreviousSmokeArea` → world position → `f4SmokeArea` texcoord.

## Shaders

- **Smoke.frag** - Deposits smoke density from per-object quads with rotation and intensity falloff. Marks output tiles in the occupancy buffer.
- **SmokeOccupancyDilate.comp** - Dilates the bit-packed occupancy buffer by Manhattan distance 2 and compacts active tile indices into the indirect dispatch buffer.
- **SmokeSpreadCommon.h** - Shared header for both spread passes. Wind influence uses two mechanisms: direct advection (shifts the base sampling coordinate in the wind direction for uniform fields) and noise-modulated displacement (scales displacement by wind noise amplitude for gradient fields). Blends between wind-displaced and stationary smoke via a retention factor.
- **SmokeSpreadOne.comp** - First ping-pong spread pass (8x8 workgroups). Reads tile index from active tile buffer, reconstructs world position from current `f4SmokeArea`, then samples the previous texture via `f4PreviousSmokeArea` so sampling is invariant to camera translation and zoom. Marks output tiles in occupancy.
- **SmokeSpreadTwo.comp** - Second ping-pong spread pass (8x8 workgroups). Same scale-aware tile-list pattern as pass one, plus terrain-elevation-based decay, edge-of-area fade-out, and constant subtraction with threshold zeroing to eliminate lingering low-density smoke.

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory overview
- [../Wind/CLAUDE.md](../Wind/CLAUDE.md) - Wind velocity field that drives smoke displacement
