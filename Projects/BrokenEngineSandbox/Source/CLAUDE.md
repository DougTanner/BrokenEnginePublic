# BrokenEngineSandbox - Sample Game Implementation

A space combat game demonstrating the full capabilities of the Broken Engine. Features fast-paced dogfighting, wave-based enemy spawning, and terrain interaction.

## IMPORTANT: Frame Purity Constraint

Frame code is purely functional. Frame updates must only rely on explicit function parameters -- Frame code must NEVER query Game (`gpGame`) for anything. The Frame does not know which Player is human and which is AI. Human player identity, camera shake, death screen transitions, and respawn orchestration are Game-level responsibilities. Frame code operates on data-driven parameters only (e.g., iterating all players to find alive ones, rather than querying Game for a player index).

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
- Frame state management via `ChangeFrame()` and `Restart()`, using `GameFlags_t` to specify target state
- Cursor behavior delegation via `ShouldTrapCursor()` and `ShouldUseCrosshair()` overrides, keeping game-specific logic out of engine code
- Menu input processing via `ProcessMenuInput()` override (called by GameBase's Template Method `PreUpdate()`)
- Human player tracking via stable `player_t` ID (`HumanPlayerId()`, `IsHumanPlayer()`, `HumanPlayerIndex()`). The human player is identified by ID, not by array index -- player indices change as players are added/removed via swap-and-pop
- Complete frame input construction via `BuildFrameInput()`: converts raw input to human player input, delegates to `PlayerAi::UpdatePlayer()` for AI wingmen input, detects human death (sets `kDeathScreen`), monitors human armor for camera shake, and buffers `kSpawnPlayer`/`kRespawnPlayer` status changes into `mPendingStatusChanges`. Status changes are drained by GameBase only when physics steps will run, preventing event loss on frames with zero full updates
- Autosave/quicksave file handling with versioned serialization
- Music playlist switching between menu and gameplay modes (separate playlists with callback-driven track progression)
- Sound settings persistence via static Save/Load/Reset methods
- 3D audio spatial configuration at startup
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
- Physics update via GameBase::UpdateFramesAndRender (only if not paused), which calls `Game::BuildFrameInput()` to construct the complete FrameInput (human input, AI wingmen input) and drains buffered status changes only when physics steps will run

**Frame Update Phases** (inherited from engine::FrameBase):
1. **Interpolate** - Time-based systems, physics simulation, movement
2. **PostRender** - Collision, spawning, and destruction

**Game vs Frame Responsibilities**: Human-specific behavior (camera shake, death detection, HUD display, respawn orchestration) lives in Game and UI code. Frame code is purely functional and operates on all players uniformly through data-driven iteration.

## Combat Systems

- **Players** - SOA collection of player spaceships (1 human + AI wingmen) using `kIdToIndex` for stable ID-based lookup (`player_t` alias). The human player is identified by the Game class via stable ID, not by array index. Supports death/respawn cycle: Game detects human death, sets `kDeathScreen`, and buffers `kRespawnPlayer` into persistent `mPendingStatusChanges` to bring the human back when physics next runs
- **PlayerAi** (`PlayerAi.h/cpp`) - Drives AI wingmen with gradient-based contour following and targeted burst fire. Uses GlobalNormal to compute terrain gradient, then steers perpendicular to it (contour direction) to patrol beach-level terrain. Alternates CW/CCW contour direction by player index. Applies elevation correction to stay near preferred beach level and mountain look-ahead to steer more aggressively when high terrain is ahead. Uses exponential interpolation for smooth turning. Returns toward island center when over open ocean or drifting too far. Finds nearest alive, visible enemy spaceship within range using both frustum visibility and terrain line-of-sight checks, then fires blasters and missiles in independent timed bursts with separate cooldowns, aiming independently of patrol movement direction. Owned by Game class; called per AI player from `Game::BuildFrameInput()` to populate wingmen input before frame updates begin
- **Blasters** - Rapid-fire projectiles with area lights and terrain collision
- **Spaceships** - AI-controlled enemies with wave-based spawning and health. Uses data-driven player targeting (`NearestAlivePlayerPosition`) rather than querying Game
- **Missiles** - Guided homing projectiles with AI tracking, visual effects, and owned area lights/pushers/smoke trails/sounds

## Configuration Files

| File | Purpose |
|------|---------|
| `Pch.h` | Compile-time feature toggles via `inline constexpr bool` (logging, debug layers, profiling, allocation tracking, validation, recording, debug input) and `#define ENABLE_CRT_DEBUG_HEAP` for CRT debug heap mode - used with `if constexpr` for zero-overhead conditional compilation. Constants are alphabetically sorted within each build configuration section |
| `Frame/HealthDamage.h` | Combat balance values and collision category/mask configuration |
| `Profile/GameProfile.h` | Performance profiling zones |
| `Version.h` | Save file version tracking |

## Build Configuration

The vcxproj uses `/fp:strict` for deterministic floating-point math across different hardware configurations, complementing the engine's SSE4-only DirectXMath configuration and FMA3 disable.

## See Also
- Engine Architecture: [../../../Engine/Source/CLAUDE.md](../../../Engine/Source/CLAUDE.md)
- Common Utilities: [../../../Common/CLAUDE.md](../../../Common/CLAUDE.md)
