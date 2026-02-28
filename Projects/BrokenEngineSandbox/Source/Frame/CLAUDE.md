# /Projects/BrokenEngineSandbox/Source/Frame/

Game-specific frame state and core game systems. Extends the engine's FrameBase with game logic for space combat.

## IMPORTANT: Frame Purity Constraint

Frame code must be purely functional. Frame updates should only ever rely on the explicit function parameters passed to them. Frame code must NEVER query Game (`gpGame`) for anything. The Frame should not need to know which Player is human and which is AI -- all such distinctions are Game-level knowledge. Human player identity, camera shake, death screen transitions, and respawn orchestration are handled by the Game class, not by Frame code. Frame code operates on data-driven parameters only (e.g., `NearestAlivePlayerPosition()` iterates all players rather than querying Game for a specific player index).

## Architecture Overview

**Phase-Separated Structure**: Frame contains FrameInterpolate and FramePostRender sub-structures for strict separation between rendering state and logic state, enabling deterministic replay.

**Hierarchical Composition**: Each level extends corresponding engine base structures and aggregates game-specific collections via `std::unique_ptr` (Players, Blasters, Missiles, Spaceships, Targets). Frame.h forward-declares all collection types and only includes engine `FrameBase.h`; collection definitions are included only where needed via `FrameCollections.h`.

**Simulation Rate**: 64 fps fixed timestep for responsive gameplay with deterministic physics.

## Core Files

### Frame.h/cpp

Aggregates game-specific state into a fully serializable structure. Game collection members (Players, Blasters, Missiles, Spaceships, Targets) are held via `std::unique_ptr` in both FrameInterpolate and FramePostRender, with only forward declarations in the header. This decouples Frame.h from game collection headers, reducing include dependencies. FrameInterpolate, FramePostRender, and Frame declare explicit destructors, move constructors, and move assignment operators (defined in Frame.cpp where the collection types are complete). Orchestrates the two-phase update pattern and dispatches collision phases to collections. Provides `GetMissileTarget()` for missile lock-on using alignment-based filtering, subscriber-count load balancing (fewest-missiles-first), and angle priority. Both FrameInterpolate and FramePostRender provide `ServerCrc()` static methods that propagate to the engine base's `ServerCrc()`, enabling cross-build (client vs server) determinism validation by excluding client-only data. Both also provide `ServerCompare()` methods that propagate to the engine base's `ServerCompare()`, performing per-field `BreakOnNotEqual` comparison on shared fields only for desync diagnosis. `Frame::ServerCompare()` dispatches to both sub-structures. Both also provide `ServerRead()` methods that propagate to the engine base's `ServerRead()`, enabling the client to deserialize server-format streams that exclude client-only fields (using `ServerCollectionRead()` for collections). `Frame::ServerRead()` dispatches to both sub-structures. Render methods (`GraphicsResources()`, `BeginRender()`, `Render()`, `EndRender()`) are client-only via `#ifdef BT_CLIENT`.

**GameFlags**: Enum controlling game state transitions (main menu, new game, death screen). Set by Game-level logic and cleared by Frame when processing status change events.

**Alignment System**: Per-frame alignment state (player and enemy IDs plus relationship map) is owned by Game and copied into frame state. Used by collision and targeting to filter friend/foe interactions.

**Spawn System**: Periodically spawns spaceships near alive players, using terrain checks and boundary clamping to find valid positions. Skipped when no alive players exist.

**World Coordinates and Transfer**: Frames use world-space coordinates keyed by `GridCoord`. `ComputeFrameArea()` offsets the base area by grid position. Frame.h defines base area constants derived from `kpfIslandPositions`. The transfer pipeline uses `IsOutOfBounds()`/`ComputeTransferDelta()`/`FrameBounds` utilities, with `TransferRequest` carrying entity state and delta grid offset. `ComputeTransferDelta()` uses `>=`/`<=` boundary checks matching `IsOutOfBounds()` so entities at the exact boundary are consistently detected as needing transfer. The transient `transferRequests` vector is excluded from serialization, CRC, and equality checks.

### FrameCollections.h

Aggregation header that includes Frame.h plus all game collection headers (Player.h, Blasters.h, Missiles.h, Spaceships.h, Targets.h). Provides `GameInterpolateCollections()` and `GamePostRenderCollections()` free functions that return `std::tie` tuples of the dereferenced `unique_ptr` collection members, plus `GameInterpolateTypes`/`GamePostRenderTypes` type lists derived from those tuples. Files that need to operate on concrete collection types include this header instead of including Frame.h and individual collection headers separately.

### Player.h/cpp

SOA collection of player spaceships supporting multiple players (1 human + AI wingmen). Uses `CollectionFlags::kIdToIndex` for stable ID-based lookup (`player_t` type alias). All players share the same update logic -- no hardcoded assumption about which index is human.

**Multi-Player Architecture**: Input is pre-populated externally by Game before `PostRender::Update()` runs. The shared update loop reads from `playerInputs[i]` uniformly for all players.

**Spawn and Respawn**: Driven by `StatusChange` events in `FrameInput`. Spawn position is offset by frame center for grid-cell correctness (uses a different position when `kbEnableAutoInput` is enabled for automated testing). Has a `SpawnInfo` overload for transfer-based spawning that preserves full gameplay state. Transfer generates a `TransferRequest` with entity ID for human player identity tracking.

**Lifecycle**: Uses `AddIndexableElement`/`RemoveIndexableElement` for ID-tracked creation and O(1) swap-and-pop removal.

**Weapon Systems**: Blasters fire from alternating barrels with angle jitter and interpolated spawn positions. Missiles spawn from alternating sides at angled directions. Both pass wind trail properties to spawned projectiles.

**Shield and Damage**: Shield absorbs damage before armor with cooldown-based regeneration. Hex shield displays directional hit indicators with intensity decay. Impact VFX at contact points.

**Owned Objects**: Each player owns a wind trail, hex shield, billboard, and sound (all client-only via `#ifdef BT_CLIENT`), created in Spawn and removed in Destroy. `HexShieldDirections`/`HexShieldIntensities` are fixed-size array wrappers enabling SOA storage of per-direction data.

**Render**: Client-only. Skeletal animation via BufferManager's skinning allocator, death shrink effect, rotation tilt from velocity.

### HealthDamage.h

Combat balance constants, collision category/mask configuration, and difficulty-scaled damage arrays. Defines `CollisionCategory` (what am I?) and `CollidesWith` (what can I hit?). Uses a single `kBlaster` category for all blasters with alignment-based friend/foe filtering.

## Update Flow

1. **Interpolate Phase**: AllocateAndCopy then Update -- integrates velocities, syncs owned objects to engine collections
2. **PostRender Phase**: AllocateAndCopy, Update, PreCollision, PostCollision, AreaDamage, Transfer, Destroy, Spawn
3. **Three-Phase Render Pipeline**: BeginRender (compute capacities, resize GPU buffers), Render (write GPU data per frame), EndRender (write indirect draw counts)

All phases propagate to engine base, Players, and game collections via `ForEach*` helpers and `Collections()` tuple.

## See Also
- Base engine frame: [../../../../Engine/Source/Frame/CLAUDE.md](../../../../Engine/Source/Frame/CLAUDE.md)
- Game object collections: [Collections/CLAUDE.md](Collections/CLAUDE.md)
