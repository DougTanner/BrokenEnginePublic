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

### Visual Effects
- **Particles** - Explosions, weapon impacts, engine trails
- **Lighting** - Dynamic area lights for explosions and abilities
- **Smoke** - Persistent damage indicators
- **Water** - Reflection and splash effects

## Project-Specific Patterns

### Frame Update Flow
The game implements engine::GameBase and follows the standard update pattern:

**Game::Update()**:
- When PAUSED: Processes input directly, handles pause menu, returns early
- When NOT PAUSED: Receives MenuInput from GameBase, processes UI/menus

**Frame Update Phases** (inherited from engine::FrameBase):
1. **Global** - Update time-based systems (cooldowns, spawning)
2. **Interpolate** - Physics simulation and movement
3. **PostRender** - Player input handling and weapon spawning
4. **Collision** - Damage resolution and object destruction

**Input Processing**:
- ProcessRawInput() called once per frame by either Game or GameBase
- FrameInput used for player controls when not paused
- MenuInput used for UI navigation in both paused and unpaused states

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
| `Game.h/cpp` | Main game class, subsystem coordination |
| `Frame/Frame.h/cpp` | Core game loop and state management |
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