# BrokenEngineSandbox - Sample Game Implementation

A space combat game demonstrating the full capabilities of the Broken Engine. Features fast-paced dogfighting, wave-based enemy spawning, and terrain interaction.

## Engine Integration

The game follows the standard engine architecture:
- **Game class** - Inherits from `engine::GameBase`, coordinates all subsystems
- **Frame system** - Game state management, inherits from `engine::FrameBase`
- **Namespace** - All game code is in the `game` namespace
- **Data Pipeline** - Assets processed by DataPacker and loaded via FileManager

## Directory Structure

### `/Frame/` - Game State and Core Systems
Phase-separated frame structures for deterministic replay, player controller, and wave spawning system.
- [Frame/CLAUDE.md](Frame/CLAUDE.md)

### `/Frame/Collections/` - Dynamic Object Management
Blasters, missiles, and spaceships using SOA layout with dynamic memory allocation.
- [Frame/Collections/CLAUDE.md](Frame/Collections/CLAUDE.md)

### `/Graphics/` - Camera and Rendering
Game-specific camera controller with menu animations and player tracking.
- [Graphics/CLAUDE.md](Graphics/CLAUDE.md)

### `/Input/` - Control System
Three-tier input processing with automatic keyboard/mouse and gamepad detection.
- [Input/CLAUDE.md](Input/CLAUDE.md)

### `/Ui/` - User Interface
Declarative widget-based menus, HUD, and settings screens.
- [Ui/CLAUDE.md](Ui/CLAUDE.md)

### `/Profile/` - Performance Profiling
Game-specific CPU counters and timers extending the engine's ProfileManager.
- [Profile/CLAUDE.md](Profile/CLAUDE.md)

## Game Class (`Game.h/cpp`)

Central game coordinator inheriting from `engine::GameBase`.

**Purpose**: Manages game lifecycle, UI state, music playlists, and frame transitions.

**Key Responsibilities**:
- Frame state management via `ChangeFrame()` and `Restart()`
- Menu input processing via `ProcessMenuInput()` override (called by GameBase's Template Method `PreUpdate()`)
- Autosave/quicksave file handling with versioned serialization
- Music playlist switching between menu and gameplay modes (separate playlists with callback-driven track progression)
- Sound settings persistence via static Save/Load/Reset methods

**UiState Enum**: Tracks current UI screen (none, pause, graphics, sound, tweaks).

**ImGui Overlay**: Separate `mbShowImGui` boolean controls ImGui debug overlay visibility, orthogonal to UiState. F3 toggles this flag without affecting the legacy UI system.

**Global Access**: `gpGame` pointer for singleton access.

## Frame Update Flow

The game implements engine::GameBase and follows the standard update pattern:

**MainThread Orchestration** (Engine/Source/Main.cpp):
- Explicit system orchestration in `while(true)` loop
- Input processing via RawInputManager
- Conversion to game-specific MenuInput and FrameInput
- UI update (separate from physics)
- Physics update via GameBase::UpdateFramesAndRender (only if not paused)

**Frame Update Phases** (inherited from engine::FrameBase):
1. **Interpolate** - Time-based systems, physics simulation, movement
2. **PostRender** - Collision, spawning, and destruction

## Combat Systems

- **Blasters** - Rapid-fire projectiles with area lights and terrain collision
- **Spaceships** - AI-controlled enemies with wave-based spawning and health
- **Missiles** - Guided homing projectiles with AI tracking, visual effects, and owned area lights/pushers/trails/sounds

## Configuration Files

| File | Purpose |
|------|---------|
| `Pch.h` | Compile-time feature toggles (debug layers, profiling, validation, recording) |
| `Frame/HealthDamage.h` | Combat balance values and collision category/mask configuration |
| `Profile/GameProfile.h` | Performance profiling zones |
| `Version.h` | Save file version tracking |

## See Also
- Engine Architecture: [../../../Engine/Source/CLAUDE.md](../../../Engine/Source/CLAUDE.md)
- Common Utilities: [../../../Common/CLAUDE.md](../../../Common/CLAUDE.md)
