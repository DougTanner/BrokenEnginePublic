<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-05T03:52:38.000Z","dependsOn":[]} -->
# Server Spawn-Rate Bounding (Free-Agent Accumulation)

**Decision plan (present options).** The landed per-fleet member cap bounds `Fleet::members` but not the actual player *mint*: a client holding a fleet at cap accumulates un-rostered "free agent" player entities, one per tick. Bounding the mint fully touches the CRC'd origin-frame path, so the choice between the options below is determinism-sensitive and must go through `/external-grill-plan` — the executing session presents the options, obtains the decision, then implements only the chosen option.

## Context

`ServerFleetManager::OnPlayerSpawned` (`ServerFleetManager.cpp:246`) caps `Fleet::members` at `kiMaxFleetMembers` = 16 (`:19`; cap check `:273-276`). But the mint and the cap live on opposite sides of the tick:

- `ServerBroadcaster::BuildFrameInputs` (`ServerBroadcaster.cpp:17`), on advancing updates only (`bAdvancing`, `:31-32`), emits one `kSpawnPlayer` StatusChange per `mClientsWaitingForSpawn` entry into the **CRC'd origin frame** (`:35-53`), calling `gpGame->GenerateGlobalId()` (`:37`) — a real player entity is minted every advancing tick per waiting entry.
- After the tick, `ServerClientManager::FinalizeNewClients` (`ServerClientManager.cpp:179-236`) pairs new origin-frame player IDs with waiting clients, records ownership into `gpServerSession->mClientOwnedPlayerIds` and `pClient->authorizedCoords` (`:226-227`), then calls `OnPlayerSpawned` (`:231`) and erases the assigned waiting entries (`:236`).
- `OnPlayerSpawned`'s cap check (`:273-276`) early-returns *after* the entity already exists and is recorded — leaving a GUID-stamped, owned, un-rostered "free agent."

So at the member cap the fleet roster is bounded but per-client owned-player count and origin-frame player count are **not**: a spammer sending spawn-into-fleet requests against a capped fleet accumulates one free agent per tick. Each tick `FinalizeNewClients` removes the assigned waiting entries, and the `QueueSpawnForClient` dedup (`ServerClientManager.cpp:25-28`, full `{client, fleet, member}` identity) only prevents duplicates *within* the queue — so a re-sent request re-queues and mints again next tick.

This is **not worse** than the pre-cap state — it strictly improves it — and free agents are handled gracefully downstream (`OnPlayerDeath`/`OnPlayerTransferred` no-op for un-rostered ids; `DetectPlayerDeaths` cleans the owned vectors independently). The residual is that owned/origin-frame player counts remain unbounded, a latent resource-growth vector.

## Design — options to present

Deliverable is an options writeup + a decision through `/external-grill-plan`, not a predetermined edit. Present:

- **A — enqueue-time gate (mint-avoiding).** Reject a spawn-into-fleet request when its target fleet is already at `kiMaxFleetMembers`, at the enqueue site — `ServerFleetManager::ProcessSpawnIntoFleetRequests` (`ServerFleetManager.cpp:94-117`, before its `QueueSpawnForClient` call at `:114`) — *before* the request reaches `BuildFrameInputs`. Leaves the CRC'd mint path untouched (nothing to reconcile). Fleet state is server-local (`mFleets`), so the roster count is evaluable at enqueue time. `ProcessRespawnInFleetRequests` (`:119-153`) already requires a dead member (`:140-148`) and replaces rather than grows the roster, so respawn stays uncapped. Likely the safest.
- **B — mint gate in `BuildFrameInputs`.** Skip the `kSpawnPlayer` push (`ServerBroadcaster.cpp:50-51`) when the waiting entry targets a fleet at cap. Touches the CRC'd origin-frame construction directly — the determinism-sensitive path. The mint is server-authoritative and the client only replays the emitted `kSpawnPlayer` StatusChange, so a server-side skip emits *no* StatusChange (no per-client divergence to reconcile) — **but this client-replay-safety must be verified, not assumed**, since `BuildFrameInputs` feeds the shared CRC.
- **C — accept + document.** Record that the current state strictly improves the pre-cap behaviour and free agents are bounded-cost / cleaned downstream; add a comment at `OnPlayerSpawned`'s cap return (`ServerFleetManager.cpp:273-276`) and close. Viable if measured accumulation cost is negligible.

For A/B, weigh: where the fleet-cap fact is authoritatively known, whether respawn (dead-member replace) must remain uncapped (Option A preserves it by construction), and — for B only — the CRC/determinism proof obligation.

## Scope contract

In scope (target **and** ceiling — smallest complete change for the one chosen option; no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way):

- The options writeup and `/external-grill-plan` decision.
- **If A:** a cap check inside `ServerFleetManager::ProcessSpawnIntoFleetRequests` only (guarding the `QueueSpawnForClient` call at `ServerFleetManager.cpp:114`), plus any mechanical necessity (constant visibility) it requires. No change to `ProcessRespawnInFleetRequests`, `QueueSpawnForClient`, `BuildFrameInputs`, or `FinalizeNewClients`.
- **If B:** a skip condition inside the `mClientsWaitingForSpawn` loop of `ServerBroadcaster::BuildFrameInputs` (`ServerBroadcaster.cpp:35-53`) plus the matching skip in `ServerClientManager::FinalizeNewClients` assignment/recording so the parallel-vector invariant holds, plus the client-replay-safety verification. No other region of either function changes.
- **If C:** one explanatory comment at the `OnPlayerSpawned` cap return (`ServerFleetManager.cpp:273-276`); no code change.

Naming a file grants no permission to touch anything in it beyond the regions named above. Everything under `## Out of scope` stays untouched regardless of option.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerBroadcaster.cpp` — `BuildFrameInputs` mint site (Option B): advancing gate `:31-32`, waiting-spawn loop `:35-53`, `GenerateGlobalId` `:37`, `kSpawnPlayer` push `:50-51`.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerClientManager.cpp` — `QueueSpawnForClient` `:17-33` (dedup `:25-28`); `FinalizeNewClients` `:179-236` (owned/authorized recording `:226-227`, `OnPlayerSpawned` call `:231`, assigned-entry erase `:236`) — the Option B lockstep-skip site.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.cpp` — `kiMaxFleetMembers` `:19`; `ProcessSpawnIntoFleetRequests` `:94-117` (Option A gate site, `QueueSpawnForClient` call `:114`); `ProcessRespawnInFleetRequests` `:119-153` (dead-member-only path, must stay uncapped); `OnPlayerSpawned` cap `:246`, `:273-276` (Option C comment site); `LookupFleetWantedCoord` `:424` (fleet-state read pattern for an enqueue-time gate).

Line numbers are current as of this revision; re-verify against source before editing.

## Risk tier and invariant exposure

Server-only (`BT_SERVER`). Tier depends on the decided option: **B is Tier 3** (touches the CRC'd `BuildFrameInputs` origin-frame construction — the central open question); **A is Tier 2** (server request-handling behavior, mint path untouched); **C is Tier 1**. No wire format or version change under any option (no new fields; the mint is server-authoritative). The parallel-vector invariant (`mClientOwnedPlayerIds` ↔ `authorizedCoords`, lockstep per `Network/Server/AGENTS.md`) must hold — a gate that skips the mint must skip the `FinalizeNewClients` recording too.

## Acceptance criteria

- **A:** with a fleet at `kiMaxFleetMembers`, repeated spawn-into-fleet requests enqueue nothing and mint no new global IDs (origin-frame player count and `mClientOwnedPlayerIds` size stable across ticks); a respawn request for a dead member of that same fleet still spawns.
- **B:** same observable bound as A, plus the client-replay-safety proof: client and server CRCs match across a capped-spawn-spam scenario, and replay of a recorded session reproduces identical CRCs.
- **C:** comment present at the cap return; no behavioral check.

## Notes

Pre-stage for `/external-grill-plan`: (a) A vs B vs C; (b) if B, the CRC/client-replay-safety proof that a server-side mint-skip introduces no client divergence. Co-schedule with the game-server-manager File Group (shares `ServerBroadcaster`/`ServerClientManager`/`ServerFleetManager`).

## Out of scope

- General inbound packet rate-limiting. Inbound packet/byte/per-type rate limiting exists at the two dispatch gates (`Server::Receive` / `ServerSession::ParseReceivedGamePackets`) via the per-poll `kiMaxClientPacketsPerTick` / `kiMaxClientInboundBytesPerTick` budgets and per-type caps (see `Documents/Architecture/Network.md` "Client → Server Contract"). The remaining gap this plan covers is narrower: bounding the CRC'd-frame player *mint* rate, which those packet-level budgets cannot reach (a legitimate request rate still mints a free agent per tick).
- The per-fleet member cap and wire-path `fNavigationDelay` validation — already in place.
- The `mClientOwnedPlayerIds`/`authorizedCoords` parallel-vector → registry refactor (`Network/Refactor_ServerClientPlayerRegistry.md`).
- Reworking downstream free-agent cleanup (already graceful).
- Any change to `QueueSpawnForClient`'s dedup semantics, `ProcessSpawnRequests` (initial no-fleet spawns), or the spawn-assignment order invariant.
