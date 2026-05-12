# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/

Player spaceships (one flagship + follower wingmen, all AI-driven). Uses `CollectionFlags::kIdToIndex` for stable ID-based lookup. All players share the same update logic — no index is privileged as the flagship.

## Non-Obvious Invariants

- **Nav mode sentinel** (packed `value + 1` into `PlayerFlags` bits 8-10): `-1` roam via `ComputeAiSteering`, `0`-`3` cardinal frame-change, `4` island destination, `5` flagship follow. Mode-4 and mode-5 RNG consumption is held identical so mode flips (mode 5 → 4 zeroes the destination at the same tick, triggering mode 4's entry-block draws) do not desync the shared random engine: mode 5 unconditionally draws two `Random(rTemplate.mfQuadFootprint, …)` per tick; mode 4 draws the same two only on the entry tick when `pVecIslandDestinations[i].W == 0.0f` (steady-state mode 4 draws zero — that's fine, both client and server see the same flags+W). Placement selection in both modes is `rStaticData.islands.at(playerIndex % rStaticData.islands.size())` so the chosen template's `mfQuadFootprint` matches.
- **Spawn random consumption**: `Spawn()` ALWAYS consumes `Random<10.0f>` for `fFrameChangeTimer` even when `SpawnInfo` pre-sets it (determinism).
- **Identity**: Parallel `engine::global_id_t` array gives stable cross-transfer, cross-session identity (preserved via `TransferData.globalPlayerId`, excluded from shared CRC). Game-layer identity uses global IDs, never local SOA indices.
- **Sizing**: `kfPlayerRadius` (in `Players.h`) is the single source of truth for all derived radii/offsets via ratio multipliers.
- **AreaDamage no-op**: `PlayersPostRender::AreaDamage` is intentional — players take damage only from direct PostCollision hits, never splash.

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md) (SOA, `Collection<T>`, PreCollision thread_local pattern, arrival grace, pending-tick countdown)
