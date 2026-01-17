# Graphics - Game Rendering Configuration

Game-specific rendering pipeline configuration for BrokenEngineSandbox.

## Overview

This directory contains the game's custom camera controller and glTF pipeline manager. The Camera class extends the engine's camera with game-specific behavior like menu animations and player tracking. GltfPipelines serves as a central coordinator for creating and recording all glTF rendering pipelines.

## Key Classes

- **Camera** - Extends `engine::CameraBase` with game-specific camera behavior including smooth position blending, orbital menu camera animation, player tracking during gameplay, camera shake effects, and controller vibration feedback. Accessed via `gpCamera`.

- **GltfPipelines** - Manages creation and recording of Vulkan pipelines for glTF model rendering. Creates shadow and main scene pipelines with appropriate flags for depth testing, culling, and indirect drawing. Accessed via `gpGltfPipelines`.

## Architecture Notes

The Camera updates independently of the frame system using its own real-time timer, enabling smooth camera motion decoupled from physics. It blends between a main menu orbital position and player-following behavior based on frame flags.

GltfPipelines follows the engine's indirect drawing pattern. Dynamic pipelines for game objects (player, spaceships, missiles) are created through the Frame system's collection classes rather than here, keeping this class focused on static test pipelines and overall coordination.

## See Also

- [Frame/Collections/CLAUDE.md](../Frame/Collections/CLAUDE.md) - Dynamic pipeline creation for game entities
- [../../../Engine/Source/Graphics/CLAUDE.md](../../../Engine/Source/Graphics/CLAUDE.md) - Engine graphics architecture
