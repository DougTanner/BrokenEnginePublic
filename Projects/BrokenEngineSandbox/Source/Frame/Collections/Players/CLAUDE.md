# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/

SOA collection of player spaceships supporting multiple players (1 human + AI wingmen). Uses `CollectionFlags::kIdToIndex` for stable ID-based lookup (`player_t` type alias). All players share the same update logic -- no hardcoded assumption about which index is human.

## File Structure

Split across three `.cpp` files sharing a single `.h`:
- **Players.cpp** - Registration, lifecycle (spawn, transfer, destroy), weapon spawning (blasters, missiles), death explosions
- **PlayersUpdate.cpp** - Movement, AI steering, collision, shield/armor damage
- **PlayersRender.cpp** - Skeletal animation and model rendering (`#ifdef BT_CLIENT` only)

Shared constants used across multiple `.cpp` files are declared in `Players.h`.

## Architecture

**Multi-Player AI**: `PostRender::Update()` computes AI behavior inline from frame state (terrain, spaceship positions) for all players uniformly. No external input is consumed during Update; the frame is self-contained. AI uses `ComputeAiSteering()` from `TerrainUtils.h` for terrain following and targets the nearest alive spaceship with line-of-sight checks.

**Spawn and Destroy**: Driven by `StatusChange` events in `FrameInput`. Spawn position is offset by frame center for grid-cell correctness. A `SpawnInfo` overload supports transfer-based spawning that preserves full gameplay state.

**Cross-Cell Transfer**: Out-of-bounds entities are flagged `kTransfer` in PostCollision, then the Transfer phase generates `TransferRequest`s (with entity ID for human player tracking) and removes the entity. A brief transfer lock timer skips AI and weapon logic to maintain smooth constant-velocity transitions.

**Shield and Damage**: Shield absorbs damage before armor with cooldown-based regeneration. Client-only hex shield displays directional hit indicators with intensity decay and impact VFX at contact points.

**Owned Objects**: Each player owns a wind trail and hex shield (client-only), created in Spawn and removed in Destroy/Transfer.

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
- Parent frame: [../../CLAUDE.md](../../CLAUDE.md)
