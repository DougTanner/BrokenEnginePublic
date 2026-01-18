# Graphics - Game Rendering Configuration

Game-specific rendering pipeline configuration for BrokenEngineSandbox.

## Overview

This directory contains the game's custom camera controller. The Camera class extends the engine's camera with game-specific behavior like menu animations and player tracking.

## Key Classes

- **Camera** - Extends `engine::CameraBase` with game-specific camera behavior including smooth position blending, orbital menu camera animation, player tracking during gameplay, camera shake effects, controller vibration feedback, and day/night cycle management via sun angle. Stores the current frame number for use by global render passes. Provides two `Update()` overloads: one taking `Frame` for standard updates, and one taking `FrameInterpolate` directly for use with the async rendering pipeline. Accessed via `gpCamera`.

## Architecture Notes

The Camera updates independently of the frame system using its own real-time timer, enabling smooth camera motion decoupled from physics. It blends between a main menu orbital position and player-following behavior based on frame flags.

Dynamic pipelines for game objects (player, spaceships, missiles) are created through the Frame system's collection classes via the engine's PipelineManager.

## See Also

- [Frame/Collections/CLAUDE.md](../Frame/Collections/CLAUDE.md) - Dynamic pipeline creation for game entities
- [../../../Engine/Source/Graphics/CLAUDE.md](../../../Engine/Source/Graphics/CLAUDE.md) - Engine graphics architecture
