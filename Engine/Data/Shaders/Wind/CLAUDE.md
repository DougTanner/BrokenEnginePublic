# Engine/Data/Shaders/Wind - Wind Simulation Shaders

GPU-driven 2D wind velocity field (RG = world-space XY, ping-pong RG16F textures) using hierarchical indirect compute dispatch, sharing the smoke system's coordinate space.

## Overview

Objects deposit wind via additively-blended quads in two modes: directional trail quads from moving objects (WindTrails collection, CPU-computed direction) and radial axis-aligned quads from explosions (WindRadials, direction computed in-shader outward from quad center). The field spreads once per frame over active tiles only; its sole consumer is the smoke spread. Wind is purely visual and client-only — driven by wall-clock delta time, never part of deterministic frame state.

Wind reads smoke's dynamic world-area uniforms (`f4SmokeArea` / `f4PreviousSmokeArea`); the render ordering contract has smoke write its globals before wind reads them.

## Architecture: Hierarchical Indirect Dispatch

Two buffer pairs match the ping-pong texture index: occupancy (bit-packed, one bit per 8x8 tile — Occupancy[i] describes Texture i's content) and active-tile lists (indirect dispatch args + packed tile indices). The record-once command-buffer invariant means both A and B variants of dilate/spread/deposit record unconditionally every frame; per-frame variation flows only through the UBO texture index (the inactive spread early-outs at runtime), indirect buffers, and the GPU-written tile lists. Each dilate reads the *other* index's occupancy — its spread's input texture — and writes its own spread's active list; crossing this produces a one-frame-stale active set.

Because the world-area shifts and scales between frames, every cross-frame lookup goes world-position-first: the dilate remaps each tile center into the previous-frame grid before dilating a 5x5 box (radius 2) to catch advected wind, and both spread passes remap texcoords via the previous area (unlike smoke, where only pass A imports cross-frame state).

## Simulation Kernel (WindSpreadCommon.h)

`WindSpread()` runs semi-Lagrangian advection (displacement clamped to 3 texels), world-position-anchored swirl noise (pattern stays put under camera motion), simplified vorticity confinement, diffusion, and frame-rate-independent decay. Two cross-cutting designs:

- **Magnitude regime**: behavior constants are Low/High pairs blended by field magnitude — weak wind stays laminar, strong wind turns turbulent; "momentum" tunables invert into less spread/swirl/diffusion.
- **Exact-zero convergence**: a tanh soft clamp bounds the field and a constant decay floor returns exact zero below it. This self-extinguishes the active-tile set (occupancy clears each frame and only non-zero tiles re-mark), keeping dispatch cost proportional to actual wind — and it is also how enabling/disabling wind "clears" the field; there is no explicit per-frame clear pass (textures are hard-cleared once at creation/recreate time C++-side).

Velocities are stored world-oriented (+Y = north); every conversion to/from UV space flips Y (advection displacement, radial deposit direction).

## Shaders

- **WindDeposit.frag** - Writes wind velocity from per-object quads with falloff and magnitude scaling. Non-zero output atomicOrs the tile's occupancy bit, seeding the active set for newly windy tiles.
- **WindSpreadOne.comp / WindSpreadTwo.comp** - Mirrored ping-pong spread passes (8x8 workgroup = one tile). A shared flag marks the output tile's occupancy once per workgroup when any texel produced non-zero wind. Each returns early when the other index is active.
- **WindOccupancyDilate.comp** - Per-tile previous-grid remap + dilation + compaction into the active tile list (see Architecture above).

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory overview
- [../Smoke/CLAUDE.md](../Smoke/CLAUDE.md) - Sole consumer of the wind field; owns the shared area uniforms
- [WindTrails](../../../Source/Frame/Collections/WindTrails/CLAUDE.md) / [WindRadials](../../../Source/Frame/Collections/WindRadials/CLAUDE.md) - Deposit-producing collections
