# Graphics - Game Camera

Game-specific camera controller for BrokenEngineSandbox. Client-only (`BT_CLIENT`).

## Overview

Extends `engine::CameraBase` with game-specific behavior: smooth player tracking during gameplay, orbital animation during the main menu, camera shake with controller vibration feedback, and a day/night cycle driven by sun angle progression.

**Global**: `gpCamera`

## Architecture Notes

**Hybrid Timing**: Camera position blending and shake decay use a fixed display-rate delta time to match FIFO presentation cadence. Sun angle updates use frame delta time from `FrameInterpolate` for deterministic day/night cycle progression.

**Game/Frame Boundary**: Camera shake intensity is set by Game (which detects armor damage on the human player), not by Frame code. During gameplay, the camera resolves the human player's position through `gpGame->HumanPlayerIndex()`.

**Velocity Extrapolation**: When the human player is not found in the current frame (e.g., during cross-cell grid transfers), the camera extrapolates from the last known position using a derived velocity.

**Async Rendering**: Two `Update` overloads support both standard Frame-based updates and direct `FrameInterpolate` updates for the async rendering pipeline.

## See Also

- [Frame/Collections/CLAUDE.md](../Frame/Collections/CLAUDE.md) - Dynamic pipeline creation for game entities
- [Engine Graphics](../../../Engine/Source/Graphics/CLAUDE.md) - Engine graphics architecture
