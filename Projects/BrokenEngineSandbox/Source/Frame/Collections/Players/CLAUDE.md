# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/

SOA collection of player spaceships supporting multiple players (1 human + AI wingmen). Uses `CollectionFlags::kIdToIndex` for stable ID-based lookup (`player_t` type alias). All players share the same update logic -- no hardcoded assumption about which index is human.

## File Structure

Split across four `.cpp` files sharing a single `.h`:
- **Players.cpp** - Registration, lifecycle (spawn, transfer, destroy), weapon spawning (blasters, missiles), death explosions
- **PlayersUpdate.cpp** - Movement, AI steering, shield/armor damage
- **PlayersCollision.cpp** - Collision detection and response
- **PlayersRender.cpp** - Skeletal animation and model rendering (`#ifdef BT_CLIENT` only)

Shared constants used across multiple `.cpp` files are declared in `Players.h`.

## Architecture

**Multi-Player AI**: All players run identical AI logic from frame state alone (no external input). Uses `ComputeAiSteering()` for terrain following and edge-crossing encouragement. Targets nearest alive spaceship with line-of-sight checks. Burst-fire timers control blaster and missile cadence.

**Weapon Mode**: `PlayerFlags::kUseMissiles` gates which weapon fires — `SpawnBlasters` runs when the flag is clear, `SpawnMissiles` when set. The mode is toggled by `kWeaponModeChange` StatusChange events (carrying a `WeaponModeChangeData` with player UUID) injected by the server session.

**Spawn/Destroy/Transfer**: Driven by `StatusChange` events in `FrameInput`. Transfer preserves full gameplay state via `TransferRequest` with entity ID, including the client GUID (carried in `TransferData` and restored on the destination cell). A transfer lock timer skips AI/weapon logic briefly after arriving in a new cell.

**Shield and Damage**: Shield absorbs damage before armor with cooldown-based regeneration. Client-only hex shield displays directional hit indicators with intensity decay and impact VFX.

**Owned Objects**: Each player owns a wind trail and hex shield (client-only), created in Spawn and removed in Destroy/Transfer.

## Client GUIDs and Global Player IDs

**PlayersPostRender**: Includes `pClientGuids` — a parallel array of GUIDs identifying which connected client controls each player. Used by `ServerSession::ResetClientsForLoad` to re-link client connections to player slots after a load.

**Global Player IDs**: A parallel array of `engine::global_player_t` values provides stable cross-transfer, cross-session identity for each player. Assigned at spawn (carried in `SpawnInfo.globalPlayerId`), preserved across cell transfers via `TransferData.globalPlayerId`, and excluded from shared CRC validation since they are server-side bookkeeping. The Players collection uses these IDs (not local SOA indices) for all game-layer identity operations.

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
- Parent frame: [../../CLAUDE.md](../../CLAUDE.md)
