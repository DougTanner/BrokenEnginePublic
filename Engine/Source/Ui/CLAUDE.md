# `/Engine/Source/Ui/` - User Interface

Runtime-adjustable parameter wrappers and ImGui-based screen classes for UI rendering.

## Overview

All UI rendering uses ImGui exclusively via ImGuiManager. This directory contains the Wrapper system for runtime-adjustable parameters and ImGui screen implementations for debug overlays.

## Key Systems

- **Wrapper** - Type-safe value container for UI-bound settings. Stores current/previous values with min/max bounds, supports float, bool, and discrete enum types. Change detection via `Changed()` method. Global Wrapper instances (declared in WrapperBase.h, defined in WrapperBase.cpp) expose runtime-adjustable parameters for graphics, audio, and gameplay tuning

## Architecture Notes

Wrapper globals provide runtime adjustment of rendering parameters (exposure, gamma, day brightness, lighting), audio settings (volumes), and visual tuning (wave counts, shadow feathering, wind simulation). These are accessed by shaders, audio, and rendering code throughout the engine. PBR-specific wrappers control model rendering with separate BRDF diffuse/specular multiplier and power controls, independent IBL diffuse/specular multiplier and power controls, IBL ambient intensity, IBL shadow blend and ambient color blend, shadow floor, tone mapping (exposure, gamma), emissive intensity, and engine lighting integration (directional lighting, lighting specular, sun). Wind wrappers control time and global scaling (time scale), propagation (advection, swirl, decay high/low for magnitude-dependent energy dissipation with intuitive slider direction, threshold low/high/power defining a range with configurable power curve for the magnitude factor that gates simulation stages, momentum high/low for directional inertia where high momentum means less lateral spread, diffusion for lateral spread, energy scale high/low for compensating numerical dissipation), integration with the smoke system (strength, smoke retention blend, smoke power), and deposit behavior (area, intensity, length multiplier). Per-entity deposit wrappers allow independent tuning of deposit area and intensity for Player, Spaceships, Player Blasters, Spaceships Blasters, and Explosions, while Missiles use the global default deposit wrappers.

## See Also

- [Screens/CLAUDE.md](Screens/CLAUDE.md) - ImGui-based debug overlays (TweaksScreen)
