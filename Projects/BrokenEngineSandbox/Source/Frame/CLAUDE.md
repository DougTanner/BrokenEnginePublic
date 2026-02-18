# /Projects/BrokenEngineSandbox/Source/Frame/

Game-specific frame state and core game systems. Extends the engine's FrameBase with game logic for space combat.

## Architecture Overview

**Phase-Separated Structure**: Frame contains FrameInterpolate and FramePostRender sub-structures for strict separation between rendering state and logic state, enabling deterministic replay.

**Hierarchical Composition**: Each level extends corresponding engine base structures and aggregates game-specific collections (Players, Blasters, Missiles, Spaceships, Targets).

**Simulation Rate**: 64 fps (15.625ms) timestep provides responsive gameplay with deterministic physics.

## Core Files

### Frame.h/cpp

Aggregates game-specific state into a fully serializable structure with strict phase separation. Orchestrates the two-phase update pattern: Interpolate phase for rendering state (positions, directions) and PostRender phase for logic state (velocities, health, AI). Manages collision flow by dispatching PreCollision/PostCollision to collections. Provides `GetMissileTarget()` for missile lock-on which prioritizes targets with fewer subscribers first (distributing missiles across enemies), then by smallest angle within the same subscriber count. Applies visibility and range filtering (45 units max) before target selection.

**FrameFlags**: Enum controlling game state transitions - `kMainMenu` for title screen, `kGame` for new game start, `kContinue` for loading autosave and resuming gameplay, and `kDeathScreen` for game over state.

**Alignment System**: Alignment IDs are owned by the Game class as members (`mPlayerAlignment`, `mEnemyAlignment`, `mAlignments`). Initialized in the Game constructor, which generates unique IDs and adds an enemy relationship between them via `Alignments::AddAlignment()`. The alignment state is then copied to frame state (`postRender.playerAlignment`, `postRender.enemyAlignment`, `postRender.alignments`). Each player stores its alignment per-instance. The `Alignments` sparse relationship map is passed to `Collision::Collide()` for filtering - objects with the same alignment do not collide, while enemies (objects with different alignments that have an enemy relationship) can collide.

**Spawn System**: Spaceship spawn interval is 0.5 seconds with spawn radius of 100 units around the player. Island elevation is checked with retry at expanded radius. Out-of-bounds spawns flip to the opposite side of the player.

### Player.h/cpp

SOA collection of player spaceships (`PlayersInterpolate`/`PlayersPostRender`) supporting multiple players (1 human + AI wingmen). Inherits from `engine::Collection` with `Members()` for automatic serialization. Player[0] is always the human-controlled player; indices 1+ are AI wingmen driven by `PlayerAi`. All players share the same update logic for movement, weapons, shields, and collision.

**Multi-Player Architecture**: Input is pre-populated externally before `PostRender::Update()` runs: `RawInputToFrameInput()` writes human input to `FrameInput::playerInputs[0]`, then `GameBase::UpdateFramesAndRender()` calls `Game::UpdateAiInput()` which invokes `PlayerAi::Update()` to populate `playerInputs[1..N]`. The shared update loop in `PostRender::Update()` reads from `rFrameInput.playerInputs[i]` uniformly for all players. `SpawnInfo` provides initial position, direction, and alignment. Initial spawn creates all players in a row with spacing, each assigned the player alignment.

**Player[0] Special Handling**: Camera shake on armor damage, death screen flag on death, and HUD display are scoped to player[0] only.

**Helper Structs**: `HexShieldDirections` and `HexShieldIntensities` are fixed-size array wrappers for per-direction hex shield data, enabling SOA storage of multi-element data per player.

**Weapon Systems**:
- Blasters fire from alternating barrels at 50ms intervals with angle jitter and interpolated spawn positions accounting for player velocity. Spawn passes player-specific trail wrapper values (width, intensity, length multiplier) to each blaster instance, with non-zero intensity enabling wind trail creation
- Missiles spawn from alternating sides at angled directions (11.25 degrees outward) at 200ms intervals

**Death Explosion**: 0.7 second animation with 5ms particle bursts, radial expansion, and trail effects.

**Shield Mechanics**: Shield absorbs damage first (before armor), triggers 2-second cooldown when depleted. Hex shield displays directional hit indicators with intensity decay. Impact VFX spawns controlled puffs and point lights at contact points.

**Owned Objects**: Each player owns a wind trail and hex shield, created in Spawn and removed in Destroy when exploding. Wind trails synced with player-specific tweakable wrappers. Hex shields synced with rotation animation and directional damage intensity decay.

**Render**: Uses BufferManager's skinning allocator for skeletal animation GPU upload. Renders all players with death shrink effect and rotation tilt from velocity.

### HealthDamage.h

Combat balance constants, collision system configuration, and damage type definitions. Defines CollisionCategory (what am I?) and CollidesWith (what types can I hit?).

**Collision Categories**: Single `kBlaster` category for all blasters - alignment filtering handles friend/foe discrimination. Other categories: `kSpaceship`, `kPlayer`, `kMissile`.

**Alignment-Based Filtering**: Objects with the same alignment do not collide (player blasters pass through player, enemy blasters pass through enemy spaceships). Different alignments with an enemy relationship trigger collision detection.

**Combat Balance Constants**:
- Player armor: 50, shield: 100 (regen: 5/sec)
- Spaceship health: 10, collision damage: 5
- Blaster damage: 6, missile damage: 30 (7 unit radius)
- Difficulty-scaled damage arrays for spaceship blasters and collisions

## Initialization Flow

During game startup, Frame implements two initialization phases:
1. **Register Phase**: `FrameInterpolate::Register()` calls static `Register()` on Players and all collection Interpolate structs for type registration (area lights, blasters, explosions, hex shields, etc.)
2. **Graphics Resources Phase**: `FrameInterpolate::GraphicsResources()` calls `GraphicsResources()` on Players and all collections for GPU pipeline and buffer allocation.

## Update Flow

1. **Interpolate Phase**:
   - **AllocateAndCopy**: Propagates to parent, Players, and all game collections
   - **Update**: Integrates velocities into positions, updates sun angle with day/night speed variation, syncs owned objects to engine collections (area lights, hex shields, sounds, trails)
2. **PostRender Phase**:
   - **AllocateAndCopy**: Propagates to parent, Players, and all game PostRender collections
   - **Update**: Processes input and AI logic
   - **PreCollision/PostCollision**: Collision layer setup and damage application
   - **AreaDamage**: Processes area-of-effect damage
   - **Destroy/Spawn**: Object lifecycle management
3. **Render Phase**: `FrameInterpolate::Render()` dispatches to engine base, Players, and all game collections for GPU buffer writes, skeletal animation evaluation, and indirect draw buffer updates.

## See Also
- Base engine frame: [../../../../Engine/Source/Frame/CLAUDE.md](../../../../Engine/Source/Frame/CLAUDE.md)
- Game object collections: [Collections/CLAUDE.md](Collections/CLAUDE.md)
