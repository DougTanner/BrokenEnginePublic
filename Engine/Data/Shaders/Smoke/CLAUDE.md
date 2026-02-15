# Engine/Data/Shaders/Smoke - Smoke Simulation Shaders

Volumetric smoke simulation using ping-pong texture buffers with wind-driven displacement.

## Overview

The smoke system maintains a density field that deposits, spreads, and decays over time. Objects deposit smoke trails, and the field propagates each frame via two alternating spread passes. The wind velocity field displaces smoke during spreading, with clamped displacement to prevent excessive distortion.

## Shaders

- **Smoke.frag** - Deposits smoke density into the smoke texture from per-object quads with rotation-based texture sampling and power-curve falloff.
- **SmokeSpreadCommon.h** - Shared header for both spread passes. Contains two functions: `SmokeWorldPosition` for reconstructing world coordinates from texcoords, and `SmokeSpread` which performs all noise computation (wind noise and swirl noise) and wind-driven advection in a single function. Wind texture is selected discretely via `fWindTextureIndex` (0.0 or 1.0) from the ping-pong buffer pair. When wind is present, displacement combines two components: noise-scaled displacement along the rescaled wind direction, and swirl-based displacement using swirl noise samples scaled by a magnitude-dependent power curve. Retention blending mixes between wind-displaced and stationary smoke.
- **SmokeSpreadOne.frag** - First ping-pong spread pass. Applies noise displacement and wind-driven spreading via SmokeSpreadCommon with zero elevation.
- **SmokeSpreadTwo.frag** - Second ping-pong spread pass. Same wind-driven spreading as pass one, plus additional decay for low-density values, terrain-based decay using elevation sampling, and edge-of-simulation-area decay.

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory overview
- [../Wind/CLAUDE.md](../Wind/CLAUDE.md) - Wind velocity field that drives smoke displacement
