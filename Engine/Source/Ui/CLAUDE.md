# `/Engine/Source/Ui/` - User Interface

Runtime-adjustable parameter wrappers and ImGui-based screen classes for UI rendering.

## Overview

All UI rendering uses ImGui exclusively via ImGuiManager. This directory contains the Wrapper system for runtime-adjustable parameters and ImGui screen implementations for debug overlays.

## Key Systems

- **Wrapper** (`WrapperBase.h/.cpp`) - Type-safe value container for UI-bound settings. Stores current/previous values with min/max bounds, supports float, bool, and discrete enum types. Change detection via `Changed()` method which returns a tuple of current, previous, and whether the value changed. Supports percent-based get/set for normalized slider control, toggle for booleans, and index-based access for discrete allowed-value lists

## Architecture Notes

Wrapper globals are declared in WrapperBase.h and defined in WrapperBase.cpp as `engine::` namespace globals. They expose runtime-adjustable parameters consumed by shaders, audio, and rendering code throughout the engine. The wrapper categories cover PBR rendering (BRDF, IBL, tone mapping, emissive), lighting and shadows, terrain and water rendering, ocean wave simulation (low/medium/high frequency), smoke simulation and rendering, wind simulation (propagation, displacement, per-entity deposits), audio volumes, particles, and hex shield effects.

Wind propagation wrappers use a High/Low pair pattern where each parameter has two wrappers mixed by a magnitude factor for magnitude-dependent behavior. Per-entity deposit wrappers allow independent tuning per entity type (Player, Spaceships, Blasters, Explosions).

## See Also

- [Screens/CLAUDE.md](Screens/CLAUDE.md) - ImGui-based debug overlays (TweaksScreen)
