# Engine/Data/Shaders/Wind - Wind Simulation Shaders

GPU-driven 2D wind field simulation using ping-pong texture buffers, sharing the smoke system's coordinate space.

## Overview

The wind system maintains a 2D velocity field (stored as RG channels) that advects over time. Objects deposit directional wind via trail-shaped quads, and the field spreads/decays each frame. The resulting wind velocity feeds back into the smoke simulation and affects visual elements like vegetation.

## Shaders

- **WindDeposit.frag** - Writes wind velocity into the wind texture from per-object oriented quads. Samples a falloff texture and applies wind direction and magnitude passed via per-vertex params from the CPU-side WindDeposits collection. The oriented vertex shader handles quad alignment to the motion direction, so the fragment shader simply scales the falloff by magnitude and direction.
- **WindSpreadOne.frag** / **WindSpreadTwo.frag** - Ping-pong advection passes that propagate and decay the wind field each frame. Uses semi-Lagrangian advection with noise-driven swirl perturbation. Clamps velocity magnitude via `f4WindTwo.z` to prevent runaway accumulation. Shares the smoke system's simulation area coordinate space.

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory overview
