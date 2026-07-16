# Server Spawn-Rate Bounding (Free-Agent Accumulation)

**Decision plan (present options).** The landed per-fleet member cap bounds `Fleet::members` but not the actual player *mint*, so a client holding a fleet at cap accumulates un-rostered "free agent" player entities one per tick. Bounding it fully touches the CRC'd origin-frame path — a determinism-sensitive design decision.

## Context

`ServerFleetManager::OnPlayerSpawned` caps `Fleet::members` at 16 (`ServerFleetManager.cpp:292`). But the mint and the cap live on opposite sides of the tick:

- `ServerBroadcaster::BuildFrameInputs` (`ServerBroadcaster.cpp:32-50`) emits a `kSpawnPlayer` StatusChange per `mClientsWaitingForSpawn` entry into the **CRC'd origin frame**, calling `GenerateGlobalId()` — a real player entity is minted every tick.
- After the tick, `ServerClientManager::FinalizeNewClients` (`ServerClientManager.cpp:179-240`) stamps the entity's `pClientGuids`, and pushes to `mClientOwnedPlayerIds` + `authorizedCoords` (`:226-227`), then calls `OnPlayerSpawned`.
- `OnPlayerSpawned`'s cap check (`:292`) early-returns *after* the entity already exists and is recorded — leaving a GUID-stamped, owned, un-rostered "free agent."

So at the member cap the fleet roster is bounded but per-client owned-player count and origin-frame player count are **not**: a spammer sending spawn-into-fleet requests against a capped fleet accumulates one free agent per tick (each tick's `FinalizeNewClients` clears the waiting queue, so the `{client, fleet, member}` spawn dedup permits one new-member spawn per tick).

This is **not worse** than the pre-cap state — it strictly improves it — and free agents are handled gracefully downstream (`OnPlayerDeath`/`OnPlayerTransferred` no-op for un-rostered ids; `DetectPlayerDeaths` cleans the owned vectors independently). The residual is that owned/origin-frame player counts remain unbounded, a latent resource-growth vector.

## Design

Deliverable is an options writeup + a decision, not a predetermined edit. Present:

- **A — enqueue-time gate (mint-avoiding).** Reject a spawn-into-fleet request when its target fleet is already at `kiMaxFleetMembers`, at `QueueSpawnForClient` / the `mClientsWaitingForSpawn` enqueue site — *before* it reaches `BuildFrameInputs`. Leaves the CRC'd mint path untouched (nothing to reconcile). Cost: the fleet-roster cap must be evaluable at enqueue time (fleet state is server-local, so it is), and respawn-into-dead-member requests must still pass. Likely the safest.
- **B — mint gate in `BuildFrameInputs`.** Skip the `kSpawnPlayer` push when the target fleet is at cap. Touches the CRC'd origin-frame construction directly — the determinism-sensitive path. The mint is server-authoritative and the client only replays the emitted `kSpawnPlayer` StatusChange, so a server-side skip emits *no* StatusChange (no per-client divergence to reconcile) — **but this client-replay-safety must be verified, not assumed**, since `BuildFrameInputs` feeds `sharedCrc`.
- **C — accept + document.** Record that the current state strictly improves the pre-cap behaviour and free agents are bounded-cost / cleaned downstream; add a note at `OnPlayerSpawned`'s cap return and close. Viable if measured accumulation cost is negligible.

For A/B, weigh: where the fleet-cap fact is authoritatively known, whether respawn (dead-member replace) must remain uncapped, and — for B only — the CRC/determinism proof obligation.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerBroadcaster.cpp` — `BuildFrameInputs` mint site (Option B; waiting-spawn loop still at `:32-50`, unaffected so far). NOTE: `Network/DeadMachinerySweep.md` has landed and removed the `mPendingPlayerDestroys` destroy loop from this function; `Agent/AgentHarness3_FrameQueriesAndInjection.md` has landed and added an agent-injection block at `:65-93`; the already-landed audit renamed `mSpawns` to `mBroadcastStatusChanges` in the same function — re-verify all citations in this function against current source before executing.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerClientManager.cpp` — `mClientsWaitingForSpawn` enqueue / `QueueSpawnForClient` (Option A); `FinalizeNewClients` owned/authorized recording (`:226-227`).
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.cpp` — `OnPlayerSpawned` cap (`:292`, the existing roster bound); `LookupFleetWantedCoord` (fleet-state read for an enqueue-time gate).

## Invariant exposure

Server-only (`BT_SERVER`). Option B touches the CRC'd `BuildFrameInputs` origin-frame construction (determinism-sensitive — the central open question); Option A avoids it. No wire format or version change under any option (no new fields; the mint is server-authoritative). The parallel-vector invariant (`mClientOwnedPlayerIds` ↔ `authorizedCoords`) must stay lockstep — a gate that skips the mint must skip the `FinalizeNewClients` recording too.

## Notes

Decision plan (present options). Pre-stage for `/external-grill-plan`: (a) A vs B vs C; (b) if B, the CRC/client-replay-safety proof that a server-side mint-skip introduces no client divergence. Co-schedule with the game-server-manager File Group (shares `ServerBroadcaster`/`ServerClientManager`/`ServerFleetManager`).

## Out of scope

- General inbound packet rate-limiting. Inbound packet/byte/per-type rate limiting exists at the two dispatch gates (`Server::Receive` / `ServerSession::ParseReceivedGamePackets`) via the per-poll `kiMaxClientPacketsPerTick` / `kiMaxClientInboundBytesPerTick` budgets and per-type caps (see `Documents/Architecture/Network.md` "Client → Server Contract"). The remaining gap this plan covers is narrower: bounding the CRC'd-frame player *mint* rate, which those packet-level budgets cannot reach (a legitimate request rate still mints a free agent per tick).
- The per-fleet member cap and wire-path `fNavigationDelay` validation — already in place.
- The `mClientOwnedPlayerIds`/`authorizedCoords` parallel-vector → registry refactor (`Network/Refactor_ServerClientPlayerRegistry.md`).
- Reworking downstream free-agent cleanup (already graceful).
