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

Central game coordinator inheriting from `engine::GameBase`. Accessed via `gpGame` singleton pointer.

**Game Lifecycle**: Manages state transitions between main menu, gameplay, and death screen via `GameFlags`. Supports new game, continue from autosave, and restart. Handles music playlist switching between menu and gameplay modes with callback-driven track progression.

**Human Player Tracking**: Identifies the human player by stable `player_t` ID rather than array index (indices shift due to swap-and-pop removal). Detects human death to trigger the death screen and monitors armor changes for camera shake. Tracks the human's grid coordinate across multi-frame transfers.

**Multi-Frame Grid Orchestration**: Manages the active set of grid coordinates (human's cell plus neighbors, with origin always active). Creates frames at missing coordinates, ensures destination frames exist for buffer swaps, and garbage-collects frames outside the active set. Builds per-coordinate `FrameInput` with human input on the human's cell and AI-only input elsewhere.

**Transfer Harvesting**: After carry-forward, reads transfer requests from active frames and spawns entities into destination cells. Records transfers into the human's cell for replay determinism. During replay playback, defers human-cell transfers to the recorded stream. Tracks human player grid coordinate migration when a player entity transfers between cells.

**Spawn Orchestration**: Buffers spawn and respawn status changes that are drained by GameBase only when physics steps will run, preventing event loss on frames with zero full updates. Uses `SpawnFlags` (flags-based) to track waiting-for-human-spawn and respawn-requested states. On respawn, resets the human's grid coordinate to origin. `Reset()` consolidates all human tracking state cleanup (player ID, spawn flags, armor, grid coordinate, pending changes).

**Persistence**: Autosave/quicksave via grid serialization (all frames plus human grid coordinate). Sound settings persisted separately. `ReplayMeta` struct captures human tracking state (grid coordinate, player ID, armor) and is persisted to a separate `.replay.meta` file for deterministic replay restore via `RestoreReplayMeta()`.

**PlayerAi** (`PlayerAi.h/cpp`): Drives AI wingmen with gradient-based terrain contour following and targeted burst fire. Owned by Game; called per AI player during frame input construction.

## Configuration Files

| File | Purpose |
|------|---------|
| `Pch.h` | Compile-time feature toggles via `inline constexpr bool` with `if constexpr` for zero-overhead conditional compilation |
| `Frame/HealthDamage.h` | Combat balance values and collision category/mask configuration |
| `Profile/GameProfile.h` | Performance profiling zones |
| `Version.h` | Save file version tracking |

## Build Configuration

The vcxproj uses `/fp:strict` for deterministic floating-point math across different hardware configurations, complementing the engine's SSE4-only DirectXMath configuration and FMA3 disable.

## See Also
- Engine Architecture: [../../../Engine/Source/CLAUDE.md](../../../Engine/Source/CLAUDE.md)
- Common Utilities: [../../../Common/CLAUDE.md](../../../Common/CLAUDE.md)
