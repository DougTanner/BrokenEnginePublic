# Graphics - Game Camera

Game-specific camera controller for BrokenEngineSandbox.

## Overview

This directory contains the game's custom camera, which extends the engine's CameraBase with game-specific behavior: smooth player tracking during gameplay, orbital animation during the main menu, camera shake with controller vibration feedback, and a day/night cycle driven by sun angle progression.

**Global**: `gpCamera`

## Key Classes

- **Camera** - Extends `engine::CameraBase`. Blends between a main menu orbital position and player-following behavior based on game flags. Manages the day/night cycle sun angle with variable-speed progression (slower near noon, faster at night). Provides a sun angle accessor with UI slider override support for graphics settings screens.

## Architecture Notes

**Hybrid Timing**: Camera position blending and shake decay use real-time for smooth motion independent of physics rate, while sun angle updates use frame delta time from `FrameInterpolate` for deterministic day/night cycle progression.

**Game/Frame Boundary**: Camera shake intensity is set by Game (which detects armor damage on the human player), not by Frame code. During gameplay, the camera resolves the human player's position through `gpGame->HumanPlayerIndex()`, which maps the stable player ID to the current array index.

**Async Rendering**: Two Update overloads support both standard Frame-based updates and direct FrameInterpolate updates for the async rendering pipeline.

## See Also

- [Frame/Collections/CLAUDE.md](../Frame/Collections/CLAUDE.md) - Dynamic pipeline creation for game entities
- [../../../Engine/Source/Graphics/CLAUDE.md](../../../Engine/Source/Graphics/CLAUDE.md) - Engine graphics architecture
