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
ImGui-based HUD, menus, and settings screens.
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
- AI input population via `UpdateAiInput()` which delegates to `PlayerAi::Update()` for wingmen input
- Autosave/quicksave file handling with versioned serialization
- Music playlist switching between menu and gameplay modes (separate playlists with callback-driven track progression)
- Sound settings persistence via static Save/Load/Reset methods
- Static collision group initialization at startup (owned by Game, initialized once in constructor)

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
- Physics update via GameBase::UpdateFramesAndRender (only if not paused), which calls `Game::UpdateAiInput()` to populate AI wingmen input before frame updates

**Frame Update Phases** (inherited from engine::FrameBase):
1. **Interpolate** - Time-based systems, physics simulation, movement
2. **PostRender** - Collision, spawning, and destruction

## Combat Systems

- **Players** - SOA collection of player spaceships (1 human + AI wingmen). Player[0] is human-controlled; indices 1+ are AI wingmen
- **PlayerAi** (`PlayerAi.h/cpp`) - Drives AI wingmen (players 1+) with random direction changes on a timer and continuous blaster fire. Owned by Game class; called from `Game::UpdateAiInput()` in `GameBase::UpdateFramesAndRender()` to populate `FrameInput::playerInputs[1..N]` before frame updates begin
- **Blasters** - Rapid-fire projectiles with area lights and terrain collision
- **Spaceships** - AI-controlled enemies with wave-based spawning and health
- **Missiles** - Guided homing projectiles with AI tracking, visual effects, and owned area lights/pushers/trails/sounds

## Configuration Files

| File | Purpose |
|------|---------|
| `Pch.h` | Compile-time feature toggles via `inline constexpr bool` (logging, debug layers, profiling, allocation tracking, validation, recording, debug input) and `#define ENABLE_CRT_DEBUG_HEAP` for CRT debug heap mode - used with `if constexpr` for zero-overhead conditional compilation. Constants are alphabetically sorted within each build configuration section |
| `Frame/HealthDamage.h` | Combat balance values and collision category/mask configuration |
| `Profile/GameProfile.h` | Performance profiling zones |
| `Version.h` | Save file version tracking |

## See Also
- Engine Architecture: [../../../Engine/Source/CLAUDE.md](../../../Engine/Source/CLAUDE.md)
- Common Utilities: [../../../Common/CLAUDE.md](../../../Common/CLAUDE.md)
