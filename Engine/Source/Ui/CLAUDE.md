# `/Engine/Source/Ui/` - User Interface

Runtime-adjustable parameter wrappers and ImGui-based screen classes for UI rendering.

## Overview

All UI rendering uses ImGui exclusively via ImGuiManager. This directory contains the Wrapper system for runtime-adjustable parameters and ImGui screen implementations for debug overlays.

## Key Systems

- **Wrapper** - Type-safe value container for UI-bound settings. Stores current/previous values with min/max bounds, supports float, bool, and discrete enum types. Change detection via `Changed()` method. Global Wrapper instances (declared in WrapperBase.h, defined in WrapperBase.cpp) expose runtime-adjustable parameters for graphics, audio, and gameplay tuning

## Architecture Notes

Wrapper globals provide runtime adjustment of rendering parameters (exposure, gamma, day brightness, lighting), audio settings (volumes), and visual tuning (wave counts, shadow feathering, wind simulation, smoke simulation). These are accessed by shaders, audio, and rendering code throughout the engine. PBR-specific wrappers control model rendering with separate BRDF diffuse/specular multiplier and power controls, independent IBL diffuse/specular multiplier and power controls, IBL ambient intensity, IBL shadow blend and ambient color blend, shadow floor, tone mapping (exposure, gamma), emissive intensity, and engine lighting integration (directional lighting, lighting specular, sun). Wind wrappers control time and global scaling (time scale, threshold low/high defining a linear range for the magnitude factor that gates simulation stages), propagation as High/Low pairs mixed by fMagFactor for magnitude-dependent behavior (advection, swirl scale/amount/speed for animated noise-driven perpendicular perturbation, vorticity confinement strength and cutoff threshold for curl-based rotational amplification, decay for energy dissipation with intuitive slider direction, momentum for directional inertia where high momentum means less lateral spread, diffusion for lateral spread), integration with the smoke system (strength, smoke retention blend, smoke power), displacement controlling how wind displaces smoke during spreading (noise-scaled displacement along wind direction, swirl-based displacement with magnitude-dependent power curve), and deposit behavior (width, intensity, length multiplier). Smoke wrappers include noise influence (`gSmokeNoiseInfluence`) controlling how much noise-based displacement blends into the wind sample direction during wind-driven smoke motion. Particle wrappers include wind strength (`gParticlesWindStrength`) controlling how strongly the wind velocity field influences particle XY velocity during GPU compute updates. Per-entity deposit wrappers allow independent tuning of deposit width, intensity, and length multiplier for Player, Spaceships, and Player Blasters (length multiplier shared between player and spaceship blasters), with Spaceships Blasters and Explosions having width and intensity only. Missiles use the global default deposit wrappers.

## See Also

- [Screens/CLAUDE.md](Screens/CLAUDE.md) - ImGui-based debug overlays (TweaksScreen)
