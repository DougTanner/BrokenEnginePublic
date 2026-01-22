# `/Engine/Source/Ui/` - User Interface

Runtime-adjustable parameter wrappers and ImGui-based screen classes for UI rendering.

## Overview

All UI rendering uses ImGui exclusively via ImGuiManager. This directory contains the Wrapper system for runtime-adjustable parameters and ImGui screen implementations for debug overlays.

## Key Systems

- **Wrapper** - Type-safe value container for UI-bound settings. Stores current/previous values with min/max bounds, supports float, bool, and discrete enum types. Change detection via `Changed()` method. Global Wrapper instances (declared in WrapperBase.h, defined in WrapperBase.cpp) expose runtime-adjustable parameters for graphics, audio, and gameplay tuning

## Architecture Notes

Wrapper globals provide runtime adjustment of rendering parameters (exposure, gamma, lighting), audio settings (volumes), and visual tuning (wave counts, shadow feathering). These are accessed by shaders, audio, and rendering code throughout the engine.

## See Also

- [Screens/CLAUDE.md](Screens/CLAUDE.md) - ImGui-based debug overlays (TweaksScreen)
