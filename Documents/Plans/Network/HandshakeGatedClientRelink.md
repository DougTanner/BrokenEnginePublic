<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-25T21:06:16.378Z","dependsOn":[]} -->
# Fix: Gate Client Relink on Completed Handshake

## Context

At baseline `374e4e8d6517d02c5cc665d03fb549823f4bd100`, `Server::Connect` creates a `ClientConnection` with its default empty `clientGuid` (`Engine/Source/Network/Server/Server.cpp`, `Connect`), then the game phase calls `ServerClientManager::NewClients` from `ServerSession::AfterNetworkPoll`. `NewClients` attempts reconnect relink and, when it finds no player, inserts the connection ID into `mProcessedClientIds`.

The accepted `ClientHello` later installs the persisted GUID and sets `bHandshakeComplete` (`Engine/Source/Network/Server/ServerReceive.cpp`, `ClientHello`), but does not clear or re-run the game-side new-client path. Consequently a reconnect reaches the server with its original world player intact, yet receives neither ownership relink nor assign/player-state packets. Runtime confirmation against that baseline sequence showed the restored server client and advancing subscriptions/ticks, one persisted player global ID, an empty client ownership/fleet list, and no reconnect relink or assign log.

`Refactor_ServerClientPlayerRegistry.md` intentionally preserves this connect behavior while consolidating relink into `ClientPlayerRegistry::RelinkFromFrames`; it does not own handshake lifecycle. This is a separate bugfix that depends on that registry path.

## Design

In `Projects/BrokenEngineSandbox/Source/Network/Server/ServerClientManager.cpp`, make `ServerClientManager::NewClients` skip every `ClientConnection` whose existing `bHandshakeComplete` is false. Place that guard before any ownership lookup, diagnostic, relink attempt, queue/processed-state decision, or other client-manager mutation.

`ServerSessionRuntime::Poll` already runs engine `Server::Poll` before game `AfterNetworkPoll`; a successfully validated first `ClientHello` sets `bHandshakeComplete` and the persisted/minted GUID before `AfterNetworkPoll` invokes `NewClients`. The next eligible `NewClients` pass therefore calls the existing `ClientPlayerRegistry::RelinkFromFrames` with a non-empty persisted GUID. Its existing global-ID sorting and `SendAssignPlayer` then `SendPlayerState(kSpawned)` order remain unchanged.

Do not add a callback, pending queue, new connection state, retry loop, or packet. The existing handshake-complete bit is the lifecycle boundary. A failed Hello removes the connection before game processing; an idempotent Hello replay remains a transport-only response because the normal processed/owned checks retain their current behavior.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerClientManager.cpp` — add the handshake-complete early continue in `NewClients` only.
- `Projects/BrokenEngineSandbox/Source/Network/Server/AGENTS.md` — document that relink/new-client processing begins only after the engine accepts ClientHello.

Read-only contract evidence:

- `Engine/Source/Network/Server/Server.h` — `ClientConnection::bHandshakeComplete` starts false.
- `Engine/Source/Network/Server/ServerReceive.cpp` — accepted `ClientHello` establishes GUID and completion state.
- `Engine/Source/Network/Server/ServerSessionRuntime.cpp` and `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — engine poll precedes `AfterNetworkPoll`/`NewClients`.

## Out of scope

- Any ClientHello, wire packet, protocol-version, save/replay, CRC, `.pack`, or `Frame::kiVersion` change.
- Engine transport callbacks or changing `ClientConnection` layout/state ownership.
- Registry design, relink sorting, assignment/player-state send order, fleet behavior, spawning policy, or retry semantics beyond preventing pre-handshake processing.
- Changes to the completed registry-refactor Plan, which is an immutable prerequisite record.

## Risk tier and invariants

**Tier 3** — this integrates game-side ownership relink with the engine transport handshake lifecycle across independently owned subsystems.

- `NewClients` must make no observable state change for an unhandshaken connection; in particular it must not insert its ID into `mProcessedClientIds`.
- Only a successfully accepted Hello may make a connection eligible for reconnect relink. The stored persistent GUID remains the relink key.
- Reconnect relink preserves the existing ascending-global-ID order and sends assignment before spawned player state for each owned player.
- No wire bytes, protocol version, deterministic Frame state, CRC, serialization, replay, or data-pack identity changes.

## Acceptance criteria

- Static inspection confirms `NewClients` has one early `!rClient.bHandshakeComplete` continue before all ownership/processed/queue/relink work; no unhandshaken connection can be marked processed. It also confirms rejected Hello removes the connection before game processing and an idempotent replay sends only the existing transport response, so neither can create a relink or change ownership.
- Static inspection of `ClientPlayerRegistry::RelinkFromFrames` confirms ascending global-ID sorting and `SendAssignPlayer` before `SendPlayerState(kSpawned)` for every relinked player.
- Debug server build succeeds.
- Harness fresh-connect scenario: after accepted Hello (including a newly minted GUID), ordinary fleet creation/spawn remains available and the client reaches the existing zero-owned-player path.
- Harness reconnect scenario: create and retain a player, disconnect only the client, then reconnect with its persisted GUID. After accepted Hello, server and client ownership/fleet views restore the original global ID and coordinate; subscription activity and tick progression continue, with no new desync, CRC, checksum, contract-violation, or fatal/error log delta relative to the scenario baseline.

## Coordination

- Directional prerequisite: `Documents/Plans/Network/Refactor_ServerClientPlayerRegistry.md` provides the single `ClientPlayerRegistry::RelinkFromFrames` call site this fix gates. Its later deletion as a completed Plan is a satisfied stale scheduler dependency, not permission to fold this lifecycle fix into that Plan.
