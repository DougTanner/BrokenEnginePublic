# Save/Load/Replay Trust-Boundary Hardening

## Context

2026-07-03 review sweep of the save/load/replay path (server-only; replay is the de-facto determinism regression harness). The grid read is *nearly* stage-then-commit and most counts go through `ValidateDeserializedCount` — but the sweep found the specific gaps where a corrupt/truncated/hand-crafted file crashes the server later instead of being rejected at the load boundary, plus one confirmed replay-playback crash. All verified against current code this session.

1. **`FrameInput` deserialization skips the validation helper** — `Input.cpp:164-168` (`operator>>(std::istream&, FrameInput&)`): `Read(iStatusCount); statusChanges.resize(iStatusCount)` with no bound (negative → `length_error`; huge → multi-GB alloc). This operator is the live replay-stream branch (`FrameInput` is non-trivially-copyable, so `DifferenceStream` routes through it). Rider: an unknown `eType` falls into `DefaultDataForType`'s `default: return TransferData{}` (`StatusChange.h:183`), so the payload read consumes the wrong byte count and silently desynchronizes the rest of the stream.
2. **`ReadGrid`'s non-throwing failure tail commits torn state, and `ServerLoad`'s caller discards the result** — `GameSaveLoad.cpp:472` ends `return fileStream.good()` with no cleanup (a silently-failed `istream::read` leaves zero defaults that pass validators; the loop completes on garbage), while the `catch` path does clean up. `ServerSession.cpp:200` (`kClientLoadRequest`) calls `gpGame->mGameSaveLoad.ServerLoad();` and drops the `bool` — the server keeps ticking a mongrel grid with connected clients' `mClientOwnedPlayerIds`/`authorizedCoords` referencing vanished entities.
3. **`Fleet::iFlagshipIndex` unbounded** — `ServerFleetManagerUtils.cpp:73` (`ReadFleet`) reads it raw (sibling counts are validated). Consumers check the upper bound only: `iFlagshipIndex < std::ssize(members) && members.at(size_t(iFlagshipIndex))` (`ServerFleetManager.cpp:290-291, 424-433`) — a negative index passes and `.at` throws minutes later, out of `ServerUpdate`, killing the server; `(iFlagshipIndex + k) % ssize` (`FleetNavigationController.cpp:199-200`) same.
4. **Replay tick `.at(coord)` on the rebuilt input map** — `GameSaveLoad.cpp:340/352` (`SyncReplayTick`): `mFrameInputs` is wiped and rebuilt from `mActiveCoords` every `ServerUpdate` (`ServerBroadcaster::BuildFrameInputs`), and `ComputeActiveSet` (`ServerSession.cpp:339-355`) includes only subscriptions + player coords + origin — **no replay force-inclusion (verified)**. A recorded coord that was subscription-only (empty cell a client watched) replayed on a headless server → `.at` throws out of the tick loop. The recording tick guards `mCoordFrames.contains` but still `.at`s the input map.
5. **`iNextGlobalId` applied before the load validates, never rolled back** — `GameSaveLoad.cpp:432-434`: raw read (could be ≤0) + `SetNextGlobalId` before the frame loop; the catch cleanup restores frames/fleets but not the counter, so a failed load leaves the fresh-fallback game minting from a garbage base (breaks the monotonic-int64 / `!= 0` conventions).
6. **`Quickload` ASSERTs on file-derived data** — `GameSaveLoad.cpp:149` `ASSERT(mCoordFrames.contains(mClientGridCoord))`: a save whose client coord isn't among its frames passes every validator then throws via ASSERT (trust-boundary input handled contra the corrupt-input policy). `ServerLoad`/`Autoload` adopt the coord unchecked.
7. **`ValidateDeserializedCountCapacity` ceiling ignores element size** — `Serialization.h:25, 53-60`: `kiMaxDeserializedCapacity` bounds *elements*; 16M × a multi-hundred-byte SOA stride × collections × coords approves multi-GB commits before `bad_alloc` degrades it.

## Design

1. `ValidateDeserializedCount(iStatusCount, /*min bytes per entry*/ sizeof(uint8_t) + 1, rStream, "FrameInput statusChanges")` before the resize; make `DefaultDataForType`'s `default:` throw `common::CorruptStreamException` when reached from deserialization (the send side never hits it — `SerializeGroup`'s switch is exhaustive).
2. Give `ReadGrid` one failure shape: on the `!fileStream.good()` tail, run the same cleanup as the catch (clear `mCoordFrames`, `ResetState` the fleet manager) before returning false. In `ServerSession`'s `kClientLoadRequest` handler, on `false` fall back the way `ServerReset` does (fresh frame + client reset) instead of discarding.
3. In `ReadFleet`: `iFlagshipIndex < 0 || iFlagshipIndex >= iMemberCount` → throw `CorruptStreamException`.
4. In `SyncReplayTick`, replace both `mFrameInputs.at(rCoord)` with `try_emplace(rCoord).first->second` (the recording-*start* path already uses this shape); replayed inputs for inactive coords then apply to the frame exactly as recorded.
5. Stage `iNextGlobalId` in a local; validate `> 0` (throw `CorruptStreamException`); apply together with the tick/time adoption after the frame loop succeeds.
6. Move the client-coord containment check inside `ReadGrid`'s try as a `CorruptStreamException` (all three callers then reject uniformly); delete the Quickload ASSERT.
7. Bound `iCapacity * iElementBytes` against a byte ceiling in division form (mirror the count check's overflow-safe style).

## Critical files

- `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp` — `ReadGrid`, `SyncReplayTick`, `Quickload`
- `Projects/BrokenEngineSandbox/Source/Input/Input.cpp` — `operator>>(std::istream&, FrameInput&)`
- `Projects/BrokenEngineSandbox/Source/Frame/StatusChange.h` — `DefaultDataForType`
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManagerUtils.cpp` — `ReadFleet`
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — `kClientLoadRequest` handler
- `Common/Serialization.h` — `ValidateDeserializedCountCapacity`

## Invariant exposure

- Server-only save/load/replay boundary; **no wire change, no `kiVersion` bump** (no byte layout changes — validation and failure-path changes only). No CRC-path arithmetic touched: a load that succeeds today still reconstructs byte-identical state; the changes only convert crash/torn outcomes on corrupt input into uniform rejection.
- Item 4 changes replay behavior only for coords absent from the active set (currently a crash); recorded-vs-live input application for active coords is unchanged.

## Out of scope

- Byte-determinism of save output and atomic-write commit integrity — sibling plan `Save/SaveFileAtomicityAndByteDeterminism.md`.
- The network-receive StatusChange codec (`DeserializeStatusChangeBatch`) — owned by `Network/StatusChangeCodecHardening.md`; item 1 covers only the replay/file stream entry.
- Fleet float/tick-field plausibility validation beyond `iFlagshipIndex` (cheap bound only if trivially colocated; NaN `fNavigationDelay` is owned by `Network/ServerTrustBoundaryHardening.md`).
- `DifferenceStreamReader::LoadDifference`'s exact-equality end detection (`mSavedEnd.iTick + 1 == iTick`) — flagged by review as a possible non-termination if the playback tick counter can skip; verify at execution and fix only if a skip path exists.
- Any client-side load/reset behavior (`ResetForServerLoad` drift is owned by `Network/Refactor_ClientResetUnification.md`).

## Acceptance criteria

- A truncated or bit-flipped save/replay file (any offset) produces a logged rejection + clean fallback state on the server — never a later `.at` throw, torn grid, or ASSERT — for all three load entries (Quickload, `kClientLoadRequest`, Autoload).
- Replaying a recording that includes a subscription-only coord on a headless server runs to the loop point without throwing.
- A known-good save/replay round-trips exactly as before (byte-identical reconstructed state).

## Notes

- Grill decision pre-staged: item 2's `kClientLoadRequest` fallback shape — recommended: mirror `ServerReset()` (fresh frame + `ResetClientsForLoad`), the only alternative being disconnect-all; confirm at grill.
- Sequencing: `ServerFleetManagerUtils.cpp` is renamed to `ServerFleetSerialization` by `Network/AuditSweepQuickWins.md`, and `ServerSession.cpp` line cites shift under `Network/Refactor_SessionBaseCollapse.md` — refresh citations if those land first (see Order.md Dependencies).
