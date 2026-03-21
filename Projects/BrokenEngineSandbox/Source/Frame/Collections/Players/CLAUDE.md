# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/

SOA collection of player spaceships supporting multiple players (1 human + AI wingmen). Uses `CollectionFlags::kIdToIndex` for stable ID-based lookup (`player_t` type alias). All players share the same update logic -- no hardcoded assumption about which index is human.

## File Structure

Split across three `.cpp` files sharing a single `.h`:
- **Players.cpp** - Registration, lifecycle (spawn, transfer, destroy), weapon spawning (blasters, missiles), death explosions
- **PlayersUpdate.cpp** - Movement, AI steering, collision, shield/armor damage
- **PlayersRender.cpp** - Skeletal animation and model rendering (`#ifdef BT_CLIENT` only)

Shared constants used across multiple `.cpp` files are declared in `Players.h`.

## Architecture

**Multi-Player AI**: All players run identical AI logic from frame state alone (no external input). Uses `ComputeAiSteering()` for terrain following and edge-crossing encouragement. Targets nearest alive spaceship with line-of-sight checks. Burst-fire timers control blaster and missile cadence.

**Spawn/Destroy/Transfer**: Driven by `StatusChange` events in `FrameInput`. Transfer preserves full gameplay state via `TransferRequest` with entity ID. A transfer lock timer skips AI/weapon logic briefly after arriving in a new cell.

**Shield and Damage**: Shield absorbs damage before armor with cooldown-based regeneration. Client-only hex shield displays directional hit indicators with intensity decay and impact VFX.

**Owned Objects**: Each player owns a wind trail and hex shield (client-only), created in Spawn and removed in Destroy/Transfer.

## Architecture

**PlayersPostRender**: Includes `pClientGuids` (`engine::ClientGuid*`) — a parallel array of GUIDs identifying which connected client controls each player. Initialized in `Spawn`, copied via `AllocateAndCopy`, and compared in `LogDifferences`. Used by `ServerSession::ResetClientsForLoad` to re-link client connections to player slots after a load.

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
- Parent frame: [../../CLAUDE.md](../../CLAUDE.md)
