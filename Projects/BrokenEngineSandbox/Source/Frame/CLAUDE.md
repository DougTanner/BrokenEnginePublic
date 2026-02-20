# /Projects/BrokenEngineSandbox/Source/Frame/

Game-specific frame state and core game systems. Extends the engine's FrameBase with game logic for space combat.

## IMPORTANT: Frame Purity Constraint

Frame code must be purely functional. Frame updates should only ever rely on the explicit function parameters passed to them. Frame code must NEVER query Game (`gpGame`) for anything. The Frame should not need to know which Player is human and which is AI -- all such distinctions are Game-level knowledge. Human player identity, camera shake, death screen transitions, and respawn orchestration are handled by the Game class, not by Frame code. Frame code operates on data-driven parameters only (e.g., `NearestAlivePlayerPosition()` iterates all players rather than querying Game for a specific player index).

## Architecture Overview

**Phase-Separated Structure**: Frame contains FrameInterpolate and FramePostRender sub-structures for strict separation between rendering state and logic state, enabling deterministic replay.

**Hierarchical Composition**: Each level extends corresponding engine base structures and aggregates game-specific collections (Players, Blasters, Missiles, Spaceships, Targets).

**Simulation Rate**: 64 fps (15.625ms) timestep provides responsive gameplay with deterministic physics.

## Core Files

### Frame.h/cpp

Aggregates game-specific state into a fully serializable structure with strict phase separation. Orchestrates the two-phase update pattern: Interpolate phase for rendering state (positions, directions) and PostRender phase for logic state (velocities, health, AI). Manages collision flow by dispatching PreCollision/PostCollision to collections. Provides `GetMissileTarget()` for missile lock-on which uses `Alignments::CanCollide()` to filter targets by alignment (only considering enemy targets), then prioritizes targets with fewer subscribers first (distributing missiles across enemies), then by smallest angle within the same subscriber count. Applies visibility and range filtering (45 units max) before target selection.

**GameFlags**: Enum controlling game state transitions - `kMainMenu` for title screen, `kGame` for new game start, `kContinue` for loading autosave and resuming gameplay, and `kDeathScreen` for game over state. `kDeathScreen` is set by `Game::BuildFrameInput()` when the human player dies and cleared by Frame when a `kRespawnPlayer` status change is processed.

**Alignment System**: Alignment IDs are owned by the Game class as members (`mPlayerAlignment`, `mEnemyAlignment`, `mAlignments`). Initialized in the Game constructor, which generates unique IDs and adds an enemy relationship between them via `Alignments::AddAlignment()`. The alignment state is then copied to frame state (`postRender.playerAlignment`, `postRender.enemyAlignment`, `postRender.alignments`). Each player stores its alignment per-instance. The `Alignments` sparse relationship map is passed to `Collision::Collide()` for filtering - objects with the same alignment do not collide, while enemies (objects with different alignments that have an enemy relationship) can collide.

**Spawn System**: Spaceship spawn interval is 0.5 seconds with spawn radius of 100 units. Spawns near any alive (non-exploding) player found by iterating the player collection -- no Game query needed. Island elevation is checked with retry at expanded radius. Out-of-bounds spawns flip to the opposite side of the player. Spawning is skipped if no alive players exist.

### Player.h/cpp

SOA collection of player spaceships (`PlayersInterpolate`/`PlayersPostRender`) supporting multiple players (1 human + AI wingmen). Uses `CollectionFlags::kIdToIndex` for stable ID-based lookup (type alias `player_t = PlayersInterpolate::id_t`). All players share the same update logic for movement, weapons, shields, and collision. There is no hardcoded assumption about which index is human -- the human player is identified by the Game class via stable ID, not by array position.

**Multi-Player Architecture**: Input is pre-populated externally before `PostRender::Update()` runs: `Game::BuildFrameInput()` resizes `playerInputs` to match current player count, calls `RawInputToFrameInput()` to write human input directly at the human player's index, then calls `PlayerAi::UpdatePlayer()` for each AI wingman. The shared update loop in `PostRender::Update()` reads from `rFrameInput.playerInputs[i]` uniformly for all players.

**Spawn and Respawn via StatusChange**: Players are spawned through `StatusChange` events carried in `FrameInput::statusChanges`. `kSpawnPlayer` creates a new player; `kRespawnPlayer` also clears the `kDeathScreen` game flag before spawning. `Game::BuildFrameInput()` pushes spawn events: immediately when no players exist, then on a 2-second timer for AI wingmen until `kiMaxPlayers` (5) is reached. AI wingmen only spawn when the human player is alive.

**Lifecycle**: Uses `AddIndexableElement`/`RemoveIndexableElement` for ID-tracked creation and O(1) swap-and-pop removal. Destroyed players are removed in `Destroy()` after their death explosion timer expires.

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
   - **Update**: Integrates velocities into positions, updates sun angle with day/night speed variation, syncs owned objects to engine collections (area lights, hex shields, sounds, smoke trails)
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
