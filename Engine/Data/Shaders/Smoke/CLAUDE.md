# Engine/Data/Shaders/Smoke - Smoke Simulation Shaders

Volumetric smoke simulation using ping-pong texture buffers with wind-driven displacement.

## Overview

The smoke system maintains a density field that deposits, spreads, and decays over time. Objects deposit smoke trails, and the field propagates each frame via two alternating spread passes. The wind velocity field displaces smoke during spreading, with clamped displacement to prevent excessive distortion.

## Shaders

- **Smoke.frag** - Deposits smoke density into the smoke texture from per-object quads with rotation-based texture sampling and power-curve falloff.
- **SmokeSpreadOne.frag** / **SmokeSpreadTwo.frag** - Ping-pong spread passes that propagate smoke using noise-based displacement and wind field sampling. Both shaders bind two wind textures and blend between them using `mix()` with `fWindTextureIndex` as a continuous interpolation factor, providing smooth wind sampling between simulation steps. Wind displacement separates direction from magnitude, applying `fWindToSmokeStrength` with power-curve scaling via `fWindToSmokePower` for non-linear wind-to-smoke response. A retention blend (`fWindSmokeRetention`) mixes between wind-displaced and stationary smoke samples to control how much smoke follows the wind vs stays in place. SmokeSpreadTwo additionally applies extra decay for low-density values, terrain-based decay, and edge-of-area decay.

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory overview
- [../Wind/CLAUDE.md](../Wind/CLAUDE.md) - Wind velocity field that drives smoke displacement
