# BrokenEngineSandbox - Sample Game Implementation

A space combat game demonstrating the full capabilities of the Broken Engine. Features fast-paced dogfighting, missile combat, and terrain interaction.

## Engine Integration

The game follows the standard engine architecture:
- **Game class** - Inherits from `engine::GameBase`, coordinates all subsystems
- **Frame system** - Game state management, inherits from `engine::FrameBase`
- **Namespace** - All game code is in the `game` namespace
- **Data Pipeline** - Assets processed by DataPacker and loaded via FileManager

## Core Gameplay Systems

### Music System
- **Dual Playlists**: Separate menu and game music playlists
- **Automatic Switching**: Music changes when transitioning between menu and gameplay
- **Menu Music**: Casual tracks (doodle, MandatoryOvertime, song18, Tyhosibzzzz)
- **Game Music**: Action tracks (S31 series - UnexpectedTrouble, HighAlert, OnPatrol, GearsofProgress)
- **Transition Points**: ChangeFrame() and Restart() handle playlist switching

### Combat Mechanics
- **Blasters** - Rapid-fire projectile weapons with terrain collision
- **Missiles** - Homing weapons with area damage and target tracking
- **Shields** - Hex-based energy shields with directional damage
- **Dash** - Quick evasion ability with invulnerability frames
- **Armor** - Temporary damage reduction buff

### AI Systems
- **Enemy Spaceships** - Wave-based spawning with increasing difficulty
- **Pathfinding** - Navmesh-based movement avoiding terrain
- **Combat Behavior** - Target acquisition, weapon firing, evasion

### Camera System
- **Two-Part Architecture**: Camera logic split between deterministic input-dependent state (in FrameInterpolate) and view-only calculations (in Camera class)
- **Input-Dependent State**: Smoothed directional offset, eye height/rotation parameters, and screen shake stored in frame for deterministic replay
- **View Calculations**: Camera class reads frame state and calculates final view matrices without processing input
- **Camera Modes**: Orbiting camera for main menu, player-following camera for gameplay

### Visual Effects
- **Particles** - Explosions, weapon impacts, engine trails
- **Lighting** - Dynamic area lights for explosions and abilities
- **Smoke** - Persistent damage indicators
- **Water** - Reflection and splash effects

## Project-Specific Patterns

### Frame Update Flow
The game implements engine::GameBase and follows the standard update pattern:

**MainThread Orchestration** (Engine/Source/Main.cpp):
- Explicit system orchestration in `while(true)` loop
- Input processing via RawInputManager
- Conversion to game-specific MenuInput and FrameInput
- UI update (separate from physics)
- Physics update via GameBase::UpdateFramesAndRender (only if not paused)
- Menu actions, save/replay, vibration, present

**Frame Update Phases** (inherited from engine::FrameBase):
1. **Interpolate** - Time-based systems, physics simulation, movement
2. **PostRender** - PostRender, collision, spawning, and destruction

**Input Processing**:
- RawInputToMenuInput() and RawInputToFrameInput() convert raw input to game-specific format
- MenuInput used for UI navigation and system commands
- FrameInput split into FrameInputHeld (continuous) and FrameInputPressed (one-shot events)
- Game class provides PreUpdate(), ShouldUpdateFrame(), ProcessMenuInput(), ProcessSavesAndReplays()
- PreUpdate() orchestrates menu input processing, determines whether to update frames, resets time on pause/unpause
- ProcessSavesAndReplays() handles save/load/replay operations and can override input for replay playback

### Object Pooling
- All dynamic objects use pre-allocated pools (see `PoolConfig.h`)
- Collections manage object lifecycles and spatial queries
- Automatic cleanup at `kfAutoDestroyDistance` from play area

### Input Handling
- Supports keyboard, mouse, and gamepad simultaneously
- Toggle/hold modes for primary fire
- Context-sensitive controls (menu vs gameplay)

### Performance Optimizations
- Island-based terrain streaming
- Frustum culling for all render objects
- Batch rendering for similar objects
- Spatial hashing for collision detection

## Key Files

| File | Purpose |
|------|---------|
| `Game.h/cpp` | Game class (inherits GameBase), provides PreUpdate, ShouldUpdateFrame, ProcessMenuInput, ProcessSavesAndReplays |
| `Camera.h/cpp` | Camera helper class for view calculations, reads deterministic state from Frame |
| `Frame/Frame.h/cpp` | Core game loop and state management, contains input-dependent camera state in FrameInterpolate |
| `Frame/Player.h/cpp` | Player controller and abilities |
| `Frame/Collections/*.h/cpp` | Dynamic object management |
| `Graphics/GltfPipelines.h/cpp` | Rendering pipeline configuration |
| `Input/Input.h/cpp` | Control mapping and input processing |
| `Ui/Ui.h/cpp` | Menus and HUD elements |

## Configuration

- **Wave System** - Configurable spawn patterns and difficulty curve
- **Damage Values** - Defined in `HealthDamage.h`
- **Pool Sizes** - Set in `PoolConfig.h` 
- **UI Layout** - Widget positioning in `Ui.cpp`

## See Also
- Engine Architecture: [../../../Engine/Source/CLAUDE.md](../../../Engine/Source/CLAUDE.md)
- Common Utilities: [../../../Common/CLAUDE.md](../../../Common/CLAUDE.md)