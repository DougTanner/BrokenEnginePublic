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

**Multi-Player AI**: All players run identical AI logic from frame state alone (no external input). The `iNavDirection` field controls the current steering mode: `-1` = roaming via `ComputeAiSteering()`, `0-3` = navigating toward a neighboring frame in that cardinal direction, `4` = navigating to a random island destination. In mode 4, a random point within the island bounds is generated, snapped outside obstacles via `NavQuerySnapToNavigable`, stored in `pVecIslandDestinations`, and then tracked each tick with `NavQueryDirection` until arrival. Once the player arrives at the island destination, a randomized frame change timer is set and begins counting down during roaming (mode -1). When the timer expires, the player transitions to a cardinal direction (0-3) to cross to a neighboring frame. On spawn, `iNavDirection` defaults to 4. For targeting, prioritizes the nearest spaceship within range that passes visibility and line-of-sight checks; firing only occurs against such targets. Look direction falls back to any alive spaceship at unlimited range if no in-range/LOS target exists. Fires continuously at in-range targets, rate-limited by weapon spawn timers.

**Weapon Mode and Navigation Delay**: `PlayerFlags::kUseMissiles` gates which weapon fires — `SpawnBlasters` runs when the flag is clear, `SpawnMissiles` when set. Both the weapon mode and navigation delay (0–10s, default 2.0f) are updated together via `kUpdatePlayer` StatusChange events (carrying an `UpdatePlayerData` with player UUID, weapon mode flag, and navigation delay) injected by the server session. Navigation delay controls the pause before AI attempts a cross-frame transition.

**Spawn/Destroy/Transfer**: Driven by `StatusChange` events in `FrameInput`. Transfer preserves full gameplay state via `TransferRequest` with entity ID, including the client GUID (carried in `TransferData` and restored on the destination cell). A transfer lock timer skips AI/weapon logic briefly after arriving in a new cell. An arrival grace period timer (carried in `TransferData`) makes arriving players invisible to spaceship targeting and behavior scans for a short window; see [Arrival Grace Period](../CLAUDE.md) in the Collections CLAUDE.md.

**Shield and Damage**: Shield absorbs damage before armor with cooldown-based regeneration. Client-only hex shield displays directional hit indicators with intensity decay and impact VFX.

**Owned Objects**: Each player owns a wind trail and hex shield (client-only), created in Spawn and removed in Destroy/Transfer.

## Client GUIDs and Global Player IDs

**PlayersPostRender**: Includes `pClientGuids` — a parallel array of GUIDs identifying which connected client controls each player. Used by `ServerSession::ResetClientsForLoad` to re-link client connections to player slots after a load.

**Global Player IDs**: A parallel array of `engine::global_player_t` values provides stable cross-transfer, cross-session identity for each player. Assigned at spawn (carried in `SpawnInfo.globalPlayerId`), preserved across cell transfers via `TransferData.globalPlayerId`, and excluded from shared CRC validation since they are server-side bookkeeping. The Players collection uses these IDs (not local SOA indices) for all game-layer identity operations.

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
- Parent frame: [../../CLAUDE.md](../../CLAUDE.md)
