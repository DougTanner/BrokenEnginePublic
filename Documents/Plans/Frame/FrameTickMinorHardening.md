# Frame Tick Minor Hardening Batch

## Context

Low-severity bundle from the 2026-07-03 Frame review sweep — items individually too small for their own plans; none is a desync source today, but several sit inside the CRC'd tick so the batch still needs replay care.

1. **NavData "not built" sentinel.** `FrameTick.cpp:33` rebuilds when `rStaticData.navData.vertices.empty() && !rStaticData.islands.empty()` — if a cell's placements legitimately yield zero nav vertices (all contours degenerate/underwater), `BuildCellNavData` re-runs every tick forever (deterministic, pure perf/alloc waste). Verify reachability against the NavBuild contour extraction; if reachable (or simply for robustness), switch to an explicit built-flag on `FrameStaticData`.
2. **Main-menu `fSpawnTimer` accumulates unboundedly.** `Frame.cpp:102` adds `fDeltaTime` unconditionally; the `kMainMenu` early-return (`Frame.cpp:369`) skips the drain loop. Benign today (menu frames never become game frames), but a trap if a menu→game flip is ever introduced (instant burst of `fSpawnTimer/0.5` spawn groups). Clamp or skip accumulation in menu mode.
3. **Player cooldown timers drift unboundedly negative.** `fShieldCooldown`/`fDestroyedExplosionTime`/`fShieldDownSoundCooldown` decrement with no clamp (`Players.cpp:723-725`). Deterministic; hazardous only to future code that adds to rather than sets them. Clamp at a floor.
4. **Spaceships scan players from inconsistent frames within one tick.** `SpaceshipsPostRender::Update` scans *previous*-frame players (`Spaceships.cpp:649-650`) while `AvoidTerrain` (`SpaceshipsNavigation.cpp:151`) and the Spawn-phase blaster block (`Spaceships.cpp:487-488`) scan *current*-frame players. Safe (Players update first, `Frame.cpp:143-146`) and deterministic, but a player dying this tick is "alive" to steering and "dead" to terrain-avoidance in the same tick. Unify the scan source (current-frame, matching the majority) or document the split at both sites.
5. **`WrapperBase.h` thread-contract comment overstates.** `WrapperBase.h:7-10` says a wrapper read inside a `Dispatch()` region "must have NO runtime writer" — the verified invariant is "no writer concurrent with the Dispatch window", which main-thread-blocking `Dispatch` guarantees (the sole writer site `ImGuiManager::Prepare` runs inline on the main thread, which is blocked inside `Dispatch` for the tick; `Multithreading.h:23-93`). The current wording falsely condemns the existing UI-bound wrapper reads in Spaceships/Players. Doc-only rewrite.
6. **Stale comment.** `SpaceshipsCombat.cpp:9` cites "sampling constants in GameUtils.cpp" — the file doesn't exist; constants live in `TerrainUtils.cpp`.
7. **Note-only rider.** `TransferData::operator==` compares the client-only `smokeTrailId` (`StatusChange.h:102-104`); no client-side comparer between a locally-built and server-deserialized `TransferData` was found, but `FrameInput` equality consumers weren't exhaustively traced. If implementation finds a live client-side comparison, exclude the field; otherwise add a one-line comment stating the server-side-equality-only contract.

## Design

Each item is independent and mechanical once its one-line verification holds; no shared machinery. Items 1-4 are code; 5-6 are comments; 7 resolves to a comment or a one-line exclusion.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/FrameTick.cpp` + `Engine/Source/Frame/FrameBase.h` (`FrameStaticData` built-flag, item 1)
- `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp` — spawn-timer accumulation (item 2)
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.cpp` — cooldown clamps (item 3)
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/Spaceships.cpp`, `SpaceshipsNavigation.cpp`, `SpaceshipsCombat.cpp` — scan source + comment (items 4, 6)
- `Common/WrapperBase.h` — contract comment (item 5)
- `Projects/BrokenEngineSandbox/Source/Frame/StatusChange.h` — `operator==` note (item 7)

## Out of scope

- The exploding-entity fire gates in the same Spaceships/Players functions — `Frame/DyingEntitiesKeepFiring.md`.
- Any NavData build-content change (item 1 is lifecycle-flag only).
- Wrapper synchronization machinery — item 5 is doc-only; the read pattern is verified safe.

## Notes

- **Invariant exposure.** Items 2-4 mutate CRC'd values (clamps/accumulation/scan source) — behavior-visible to replays even when semantically inert; land as one batch so a single replay-invalidation event covers them, and co-schedule with `Frame/DyingEntitiesKeepFiring.md` (same files, same class of small CRC'd behavior change). Item 1's flag lives on per-cell derived data outside the CRC. Items 5-7 are docs/comments. No wire/`kiVersion` changes.
- No open grill decisions; item 4's pick (unify vs document) is a trivial choice — prefer unify-to-current unless the diff turns non-local.
