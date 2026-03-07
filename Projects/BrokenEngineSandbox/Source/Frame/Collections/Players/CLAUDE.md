# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/

SOA collection of player spaceships supporting multiple players (1 human + AI wingmen). Uses `CollectionFlags::kIdToIndex` for stable ID-based lookup (`player_t` type alias). All players share the same update logic -- no hardcoded assumption about which index is human.

## File Structure

The implementation is split across three `.cpp` files:
- **Players.cpp** - Registration, lifecycle (transfer, destroy, spawn), equality. Spawn phase delegates to static helpers: `ProcessSpawnStatusChanges()`, `SpawnBlasters()`, `SpawnMissiles()`, `SpawnDeathExplosions()`
- **PlayersUpdate.cpp** - Update, AI, collision, damage
- **PlayersRender.cpp** - Render (`#ifdef BT_CLIENT` only)

Shared constants used across multiple `.cpp` files are declared in `Players.h`.

## Architecture

**Multi-Player Architecture**: `PostRender::Update()` computes AI behavior inline from frame state (terrain elevation/normals, spaceship positions) for all players uniformly. No external input is consumed during Update; the frame is self-contained.

**Spawn, Respawn, and Destroy**: Driven by `StatusChange` events in `FrameInput`. `kDestroyPlayer` removes the player entity by ID (packed into the position vector), cleaning up owned client-only objects before removal. Spawn position is offset by frame center for grid-cell correctness. Has a `SpawnInfo` overload for transfer-based spawning that preserves full gameplay state. Transfer generates a `TransferRequest` with entity ID for human player identity tracking.

**Lifecycle**: Uses `AddIndexableElement`/`RemoveIndexableElement` for ID-tracked creation and O(1) swap-and-pop removal.

**Weapon Systems**: Blasters fire from alternating barrels with angle jitter and interpolated spawn positions. Missiles spawn from alternating sides at angled directions. Both pass wind trail properties to spawned projectiles.

**Shield and Damage**: Shield absorbs damage before armor with cooldown-based regeneration. Hex shield displays directional hit indicators with intensity decay. Impact VFX at contact points.

**AI Behavior**: Computed inline in `Update()` from frame state. Steering uses `ComputeAiSteering()` from `TerrainUtils.h` for terrain contour following and edge-crossing encouragement. Target acquisition finds the nearest alive spaceship within range using `HasLineOfSight()` terrain checks. Burst-fire timers control blaster and missile engagement. Per-player AI state is stored as SoA members, initialized in Spawn.

**Owned Objects**: Each player owns a wind trail, hex shield, billboard, and sound (all client-only via `#ifdef BT_CLIENT`), created in Spawn and removed in Destroy. `HexShieldDirections`/`HexShieldIntensities` are fixed-size array wrappers enabling SOA storage of per-direction data.

**Render**: Client-only (`PlayersRender.cpp` is entirely `#ifdef BT_CLIENT`-gated). Skeletal animation via BufferManager's skinning allocator, death shrink effect, rotation tilt from velocity.

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
- Parent frame: [../../CLAUDE.md](../../CLAUDE.md)
