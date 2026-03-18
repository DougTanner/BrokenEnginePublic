# Engine/Data/Shaders/Smoke - Smoke Simulation Shaders

Volumetric smoke simulation using ping-pong texture buffers with wind-driven displacement and hierarchical indirect dispatch.

## Overview

The smoke system maintains a density field that deposits, spreads, and decays over time. Objects deposit smoke trails, and the field propagates each frame via two alternating spread passes driven by **hierarchical indirect dispatch**: only tiles marked active in a bit-packed occupancy buffer are processed, eliminating GPU work on empty regions. The wind velocity field displaces smoke during spreading, with clamped displacement to prevent excessive distortion.

## Shaders

- **Smoke.frag** - Deposits smoke density into the smoke texture from per-object quads with rotation and intensity falloff. Also writes to the occupancy buffer (binding 3 SSBO) to mark output tiles as active for the next dispatch.
- **SmokeOccupancyDilate.comp** - Dilate + compact shader. Reads the bit-packed occupancy buffer, dilates active tiles by one tile radius to cover spreading neighbors, and writes a compacted flat list of active tile indices into the active tile buffer. The active tile buffer also serves as the indirect dispatch buffer.
- **SmokeSpreadCommon.h** - Shared header for both spread passes. Reconstructs world positions from texcoords and performs wind-driven advection combining wind noise displacement with swirl noise perturbation. Reads the active wind texture from the ping-pong buffer pair and blends between wind-displaced and stationary smoke via a retention factor.
- **SmokeSpreadOne.comp** - First ping-pong spread pass as a compute shader (8x8 workgroups). Each workgroup reads its flat tile index from the active tile buffer using `uiSmokeTilesX` for 2D reconstruction, then reconstructs the world position from the axis-aligned quad storage buffer to handle camera movement offset and applies wind-driven spreading via SmokeSpreadCommon. Marks output tiles in the occupancy buffer. Writes result via `imageStore`.
- **SmokeSpreadTwo.comp** - Second ping-pong spread pass as a compute shader (8x8 workgroups). Same tile-list lookup pattern as pass one. Applies wind-driven spreading plus three decay mechanisms: accelerated decay for low-density smoke, terrain-elevation-based decay, and edge-of-simulation-area fade-out. Also applies constant subtraction (`kfSmokeConstantDecay = 0.001f`) followed by threshold zeroing (`kfSmokeZeroThreshold = 0.005f`) to eliminate lingering low-density smoke. Marks output tiles in the occupancy buffer. Writes result via `imageStore`.

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory overview
- [../Wind/CLAUDE.md](../Wind/CLAUDE.md) - Wind velocity field that drives smoke displacement
