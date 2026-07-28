<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-25T16:52:44.000Z","dependsOn":[]} -->
# Drop Queued Spawns Whose Fleet Was Deleted

## Context

A spawn-into request is validated when it drains, but the resulting spawn is applied later. `ProcessSpawnIntoFleetRequests` confirms the target fleet exists and calls `QueueSpawnForClient`, which appends a `ClientSpawnInfo` to `ServerClientManager::mClientsWaitingForSpawn`. That queue is consumed in `ServerBroadcaster::BuildFrameInputs`, and only on an advancing update:

```cpp
const bool bAdvancing = gpGame->mfLastDeltaTime > 0.0f;
if (bAdvancing)
{
    for (const ClientSpawnInfo& rClientSpawnInformation : ...mClientsWaitingForSpawn)
```

So on a paused or otherwise zero-tick update the entry waits. A fleet delete arriving in that window succeeds, because `ProcessDeleteFleetRequests` only requires the fleet to be empty and a fleet with a queued-but-unapplied spawn still has no members.

When the update finally advances, the fleet metadata lookup fails and returns a default-constructed result, but the spawn is emitted regardless:

```cpp
// ServerBroadcaster.cpp — metadata lookup is conditional, the emit is not
if (rClientSpawnInformation.fleetGuid.IsValid())
{
    ServerFleetManager::FleetLookupResult result = ...LookupFleetWantedCoord(...);
    ...
}

StatusChange spawnChange {.eType = StatusChangeType::kSpawnPlayer, ...};
gpGame->mFrameInputs...statusChanges.push_back(spawnChange);
```

The player is spawned with default flagship state and a default wanted coordinate, attached to no fleet. `ServerFleetManager::OnPlayerSpawned` then discovers the missing fleet and returns early, so nothing repairs the attachment.

This is pre-existing and was not introduced by the completed FleetGuid request re-key. Before that change the queue stored a fleet *index*: a delete either pushed the index out of range, producing this same default-data spawn, or shifted it onto a **different** fleet, producing a wrong-fleet spawn. Keying on `FleetGuid` removed the wrong-fleet outcome and made the failure uniformly "fleet not found"; it did not add drop-on-missing semantics. That change's own acceptance criterion covers dropping a *request* whose guid no longer resolves, which `ProcessSpawnIntoFleetRequests` does correctly — this plan covers the later window, after the request has already been accepted and queued.

## Design

When a queued spawn names a fleet (`fleetGuid.IsValid()`) that no longer resolves at consumption time, drop that queue entry instead of emitting a fleet-less `kSpawnPlayer`.

Two constraints make this less trivial than skipping the emit:

- **Distinguish not-found from found-with-defaults.** `LookupFleetWantedCoord` currently returns a default-constructed `FleetLookupResult` both when the fleet is missing and, in principle, for a fleet whose own state is default. The caller cannot currently tell those apart. Give the result an explicit found/not-found signal, or have the function report resolution failure separately. Do not infer it from the payload fields.
- **Preserve the spawn-assignment pairing.** Spawn assignment diffs origin player IDs across the tick and pairs the new IDs with waiting clients **in request order** (`Network/Server/AGENTS.md`), and `RefreshPreSpawnSnapshot` / `FinalizeNewClients` depend on that correspondence. Emitting one fewer `kSpawnPlayer` than there are entries in `mClientsWaitingForSpawn` would misalign that zip and mis-assign players to clients. The dropped entry must therefore be **removed from the queue**, not merely skipped during emission, and the removal must not reorder the surviving entries.

A queue entry with an invalid `fleetGuid` is a legitimate non-fleet spawn (initial spawn, respawn request) and must continue to emit unchanged.

Log the drop at `kWarning`: the client asked to spawn and will not, which is worth investigating.

Decide during implementation whether the removal belongs in `BuildFrameInputs` before the emit loop or in a small pre-pass on `ServerClientManager`; the owning side is the one that can remove from the queue without a second traversal.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerBroadcaster.cpp` — the advancing-update consumption loop and the unconditional `kSpawnPlayer` emit.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerClientManager.h` / `.cpp` — `ClientSpawnInfo`, `mClientsWaitingForSpawn`, and the order-sensitive pairing in `RefreshPreSpawnSnapshot` / `FinalizeNewClients`.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.h` / `.cpp` — `FleetLookupResult` and `LookupFleetWantedCoord`, which must report resolution failure explicitly.

## Out of scope

- The fleet request decode path and the four request packets. `ProcessSpawnIntoFleetRequests` already drops an unresolvable guid correctly; the request leg is done.
- Preventing the delete. Deleting an empty fleet with a pending spawn is legitimate; the client cannot know a spawn is mid-flight. The consumption side must tolerate it.
- The advancing-only drain and the paused-deferral design generally. That deferral is deliberate and documented; this plan does not change when the queue drains.
- `PendingFlagshipUpdate`, which already takes the existing `continue` when its guid does not resolve.
- Respawn (`iMemberIndex >= 0`) member-identity semantics — members are never reordered and are out of scope here.

## Risk tier and invariants

**Tier 3** — the change touches spawn assignment, whose ordering feeds `StatusChange`s into CRC'd deterministic frame state.

- The emitted `kSpawnPlayer` sequence must remain in queue order, and the count must continue to match the waiting-client set the pairing consumes. This is the invariant most at risk.
- No behavior change for a queued spawn whose fleet still exists, or for a non-fleet spawn with an invalid guid.
- `StatusChange` content and ordering feed PostRender CRC and replay; dropping a spawn changes emitted state in the failure case only, and must not perturb the success case.
- The consumption loop runs under existing allocation suppression; removal from the queue must not introduce untracked heap work.

## Acceptance criteria

- Queue a spawn-into for an empty fleet, pause the server, delete that same fleet, unpause: no player is spawned, a `kWarning` records the drop, and no player exists outside a fleet.
- Same sequence but deleting a *different* fleet: the spawn still lands in its original target fleet, unchanged from current behavior.
- Ordinary spawn, respawn, and initial-connection spawn paths are unchanged, including when several clients are waiting simultaneously — each player is assigned to the correct client.
- Replay determinism check passes; per-tick CRCs match.

## Notes

- Found by adversarial review during the completed FleetGuid request re-key, which closed the wrong-fleet half of this window. The remaining half is the fleet-less spawn recorded here.
- **Confirmed at runtime** during that change's harness verification, not merely by inspection. Queueing a spawn-into while paused, deleting the same fleet while still paused, then unpausing produced:

  ```
  CLIENT [Tick: 22818] ClientSession::SendSpawnIntoFleetRequest Fleet: (3790565050252567466,16444745970584627309)
  SERVER [Tick: 22827] ServerFleetManager::ProcessSpawnIntoFleetRequests Client: 1 FleetGuid: (3790565050252567466,16444745970584627309)
  CLIENT [Tick: 22824] ClientSession::SendDeleteFleetRequest Fleet: (3790565050252567466,16444745970584627309)
  SERVER [Tick: 22827] ServerFleetManager::ProcessDeleteFleetRequests Client: 1 FleetGuid: (3790565050252567466,16444745970584627309) FleetCount: 4
  SERVER [Tick: 22828] ServerSession::SendPlayerState State: Spawned Client: 1 GlobalPlayer: 6 Grid: (0,0)
  ```

  `GlobalPlayer 6` spawned attached to no fleet. A membership sweep of the four surviving fleets confirmed none was mis-mutated, and no contract violation was recorded — the spawn is orphaned, not misdirected. This is the exact reproducer for the first acceptance criterion.
- The pairing constraint is the reason this is Tier 3 rather than a two-line guard. A naive `continue` in the emit loop compiles, passes a casual test with one waiting client, and silently mis-assigns players as soon as two clients are waiting.
