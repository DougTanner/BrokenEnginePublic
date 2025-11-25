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
Blasters and spaceships using SOA layout with dynamic memory allocation.
- [Frame/Collections/CLAUDE.md](Frame/Collections/CLAUDE.md)

### `/Graphics/` - Rendering Pipelines
GltfPipelines manager for player, enemies, and projectile rendering with shadow support.
- [Graphics/CLAUDE.md](Graphics/CLAUDE.md)

### `/Input/` - Control System
Three-tier input processing with automatic keyboard/mouse and gamepad detection.
- [Input/CLAUDE.md](Input/CLAUDE.md)

### `/Ui/` - User Interface
Declarative widget-based menus, HUD, and settings screens.
- [Ui/CLAUDE.md](Ui/CLAUDE.md)

## Game Class (`Game.h/cpp`)

Central game coordinator inheriting from `engine::GameBase`.

**Purpose**: Manages game lifecycle, UI state, music playlists, and frame transitions.

**Key Responsibilities**:
- Frame state management via `ChangeFrame()` and `Restart()`
- Menu input processing via `PreUpdate()` and `ProcessMenuInput()`
- Autosave/quicksave file handling
- Music playlist switching between menu and gameplay modes
- Sound settings persistence

**UiState Enum**: Tracks current UI screen (none, pause, graphics, sound, tweaks).

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
- **Missiles** - Placeholder for future homing projectile system

## Configuration Files

| File | Purpose |
|------|---------|
| `Frame/PoolConfig.h` | Object pool sizes and limits |
| `Frame/HealthDamage.h` | Combat balance values (reserved) |
| `Profile/GameProfile.h` | Performance profiling zones |
| `Version.h` | Save file version tracking |

## See Also
- Engine Architecture: [../../../Engine/Source/CLAUDE.md](../../../Engine/Source/CLAUDE.md)
- Common Utilities: [../../../Common/CLAUDE.md](../../../Common/CLAUDE.md)
