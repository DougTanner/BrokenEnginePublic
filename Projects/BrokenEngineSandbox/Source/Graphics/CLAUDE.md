# Graphics - Game Rendering Configuration

Game-specific rendering pipeline configuration for BrokenEngineSandbox.

## Overview

This directory contains the game's custom camera controller. The Camera class extends the engine's camera with game-specific behavior like menu animations and player tracking.

## Key Classes

- **Camera** - Extends `engine::CameraBase` with game-specific camera behavior including smooth position blending, orbital menu camera animation, player tracking during gameplay, camera shake effects, controller vibration feedback, and day/night cycle management via sun angle (private member, initialized to `kfDefaultSunAngle`). `SunAngle()` accessor returns the current sun angle, applying the UI override from `gSunAngleOverride` when in Graphics UI or ImGui overlay mode; the `bInternalOnly` parameter bypasses the override to return the raw internal value. `ResetSunAngle()` restores the sun angle to its default value for world resets. Stores the current frame number for use by global render passes. Provides two `Update()` overloads: one taking `Frame` for standard updates, and one taking `FrameInterpolate` directly for use with the async rendering pipeline. Accessed via `gpCamera`.

## Architecture Notes

The Camera uses a hybrid timing approach: camera position blending and shake decay use real-time for smooth motion independent of physics, while sun angle updates use frame delta time from `FrameInterpolate` for deterministic day/night cycle progression. The camera blends between a main menu orbital position and player-following behavior based on frame flags.

Dynamic pipelines for game objects (player, spaceships, missiles) are created through the Frame system's collection classes via the engine's PipelineManager.

## See Also

- [Frame/Collections/CLAUDE.md](../Frame/Collections/CLAUDE.md) - Dynamic pipeline creation for game entities
- [../../../Engine/Source/Graphics/CLAUDE.md](../../../Engine/Source/Graphics/CLAUDE.md) - Engine graphics architecture
