# Engine/Data/Shaders/Smoke - Smoke Simulation Shaders

Volumetric smoke simulation using ping-pong texture buffers with wind-driven displacement.

## Overview

The smoke system maintains a density field that deposits, spreads, and decays over time. Objects deposit smoke trails, and the field propagates each frame via two alternating spread passes. The wind velocity field displaces smoke during spreading, with clamped displacement to prevent excessive distortion.

## Shaders

- **Smoke.frag** - Deposits smoke density into the smoke texture from per-object quads with rotation and intensity falloff.
- **SmokeSpreadCommon.h** - Shared header for both spread passes. Reconstructs world positions from texcoords and performs wind-driven advection combining wind noise displacement with swirl noise perturbation. Reads the active wind texture from the ping-pong buffer pair and blends between wind-displaced and stationary smoke via a retention factor.
- **SmokeSpreadOne.frag** - First ping-pong spread pass. Applies noise displacement and wind-driven spreading via SmokeSpreadCommon without elevation awareness.
- **SmokeSpreadTwo.frag** - Second ping-pong spread pass. Same wind-driven spreading as pass one, plus three additional decay mechanisms: accelerated decay for low-density smoke, terrain-elevation-based decay, and edge-of-simulation-area fade-out.

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory overview
- [../Wind/CLAUDE.md](../Wind/CLAUDE.md) - Wind velocity field that drives smoke displacement
