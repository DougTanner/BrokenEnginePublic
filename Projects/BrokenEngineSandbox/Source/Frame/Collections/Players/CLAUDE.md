# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/

Player spaceships (1 human + AI wingmen). Uses `CollectionFlags::kIdToIndex` for stable ID-based lookup. All players share the same update logic — no index is privileged as "human".

## Non-Obvious Invariants

- **Nav mode sentinel** (packed `value + 1` into `PlayerFlags` bits 8-10): `-1` roam via `ComputeAiSteering`, `0`-`3` cardinal frame-change, `4` island destination, `5` flagship follow. The scheme is load-bearing because each mode consumes a fixed number of randoms per tick — mode 5 MUST consume the same two randoms (`Random<kfIslandWidth>`, `Random<kfIslandHeight>`) that mode 4 consumes, or mode-switching desyncs the random engine across clients.
- **Spawn random consumption**: `Spawn()` ALWAYS consumes `Random<10.0f>` for `fFrameChangeTimer` even when `SpawnInfo` pre-sets it (determinism).
- **Identity**: Parallel `engine::global_id_t` array gives stable cross-transfer, cross-session identity (preserved via `TransferData.globalPlayerId`, excluded from shared CRC). Game-layer identity uses global IDs, never local SOA indices.
- **Sizing**: `kfPlayerRadius` (in `Players.h`) is the single source of truth for all derived radii/offsets via ratio multipliers.
- **AreaDamage no-op**: `PlayersPostRender::AreaDamage` is intentional — players take damage only from direct PostCollision hits, never splash.

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md) (SOA, `Collection<T>`, PreCollision thread_local pattern, arrival grace, pending-tick countdown)
