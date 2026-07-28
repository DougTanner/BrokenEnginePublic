<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-24T00:00:00.000Z","dependsOn":[]} -->
# Spawn-Request Engine Surface Removal

## Context

`engine::PacketType::kClientSpawnRequest` (`NetworkProtocol.h:20`), `engine::ClientRequestFlags::kSpawnRequested`/`kRespawnRequested` (`NetworkProtocol.h:37-42`), and `engine::PendingSpawnRequest` (`ServerTypes.h:29-33`) encode spawn/respawn semantics that live entirely in the game layer — the engine only queues the request and forwards it. Per the hub rule, an engine *type* naming a game concept is the real-violation direction.

The whole path is also dead:

- `Client::SendSpawnRequest` (`ClientSend.cpp:64-81`, declared `Client.h:90`) has **no in-repo caller**; only the declaration and definition exist.
- Live spawn/respawn already flows through two working game packets, `kClientSpawnIntoFleetRequest` and `kClientRespawnInFleetRequest`, issued from `HudScreen.cpp:346` and `:362` via `ClientSession::SendSpawnIntoFleetRequest` / `SendRespawnInFleetRequest`.
- `ServerClientManager::ProcessSpawnRequests` (`ServerClientManager.cpp:35-59`) therefore iterates an always-empty `mPendingSpawnRequests` once per `ServerSession::AfterNetworkPoll` (call site `ServerSession.cpp:293`) and does nothing.

Current surface:

- Engine send: `Client::SendSpawnRequest` (`ClientSend.cpp:64-81`), declared `Client.h:90`.
- Engine receive/dispatch: `Server::ClientSpawnRequest` (`ServerReceive.cpp:116-140`), dispatch case `Server.cpp:280-281`, storage `Server::mPendingSpawnRequests` (`Server.h:181`), method decl `Server.h:210`, per-connection clears `Server.cpp:80` and `ServerSessionRuntime.cpp:121`.
- Engine types: `PendingSpawnRequest` (`ServerTypes.h:29-33`) + `ClientRequestFlags`(`_t`) (`NetworkProtocol.h:37-42`); `PacketTypeName` entry (`NetworkProtocol.h:52`); `GetClientPacketContract` row (`NetworkProtocol.h:165`).
- Engine message struct: `ClientSpawnRequestMessage` (`NetworkMessages.h:359-372`).
- Game consumer: `ServerClientManager::ProcessSpawnRequests` (`ServerClientManager.cpp:35-59`) iterates `gpServerSession->mpRuntime->mpServer->mPendingSpawnRequests` directly and reads the flags. (There is no `DrainPendingSpawnRequests` method; the game reads the vector member.)

### Deletion, not relocation

Relocating the type into the game packet space (a new `GamePacketType` enumerator, a game-side pending-spawn queue, a client send wrapper, a server decode branch) spends an incompatible wire change to rebuild a transport path that nothing sends and nothing needs — a speculative extension point, against the minimum-sufficient-change directive. Deleting the surface resolves the layer violation just as completely and leaves no husk to maintain. If a flag-byte spawn request is ever wanted, it gets designed against a live caller then.

### The enum hole is itself a wire change

`PacketType` is `uint8_t` and enumerator order **is** the packet type byte. `kClientSpawnRequest` sits mid-enum at `NetworkProtocol.h:20` (value 4), so removing it shifts every later engine enumerator down by one — including `kGamePacketStart` (17 → 16). `GamePacketType::kServerAssignPlayer` is defined as `static_cast<uint8_t>(engine::PacketType::kGamePacketStart)` (`GamePacketType.h:8`), so the entire game packet space renumbers with it. Engine and game enums are **not** separate byte spaces.

Conclusion: this deletion is an incompatible wire change and requires a `kuiProtocolVersion` bump beyond 7, exactly as a relocation would have. Do not dodge the bump with a reserved placeholder enumerator — a numbering hole kept only so an old peer still parses is backward compatibility, which needs explicit user consent this plan does not have. Delete outright, accept the renumber, bump the version.

## Design

Straight deletion. No new `GamePacketType` enumerator, no game-side pending-spawn queue, no client send wrapper, no server decode branch.

- Delete the engine surface enumerated in Context: `kClientSpawnRequest`, `ClientRequestFlags`(`_t`), `PendingSpawnRequest`, `ClientSpawnRequestMessage`, `Client::SendSpawnRequest`, `Server::ClientSpawnRequest` + its dispatch case + `mPendingSpawnRequests` + both clears, and the `PacketTypeName` / `GetClientPacketContract` entries.
- Delete `ServerClientManager::ProcessSpawnRequests` and its sole call site. Its only input is the deleted vector, so keeping it would leave an empty function called once per poll. `QueueSpawnForClient`, `mClientsWaitingForSpawn`, `mDeadClientIds`, and `mProcessedClientIds` all stay — the live spawn-into-fleet / respawn-in-fleet path owns them.
- Bump `kuiProtocolVersion` past 7 (Coordination governs sharing one bump).
- Update `Documents/Architecture/Network.md`: drop the `kClientSpawnRequest` contract row, update the current-version note.

## Critical files

- `Engine/Source/Network/NetworkProtocol.h` — `PacketType` enum, `ClientRequestFlags`, `PacketTypeName`, `GetClientPacketContract`, `kuiProtocolVersion`.
- `Engine/Source/Network/NetworkMessages.h` — `ClientSpawnRequestMessage` deletion site.
- `Engine/Source/Network/Server/{Server.h,Server.cpp,ServerReceive.cpp,ServerTypes.h,ServerSessionRuntime.cpp}` — receive handler, dispatch case, pending vector, clears, `PendingSpawnRequest`.
- `Engine/Source/Network/Client/{Client.h,ClientSend.cpp}` — the uncalled send method.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerClientManager.{h,cpp}` — `ProcessSpawnRequests` and the comment that names it.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — the `ProcessSpawnRequests` call site only.
- `Documents/Architecture/Network.md` — packet contract table and wire-protocol version note.

## Scope contract

The listed scope is both target and ceiling. Make the smallest complete change that satisfies the acceptance criteria and invariants. Add no abstractions, configuration, extension points, replacement packet, or refactors; do not "clean up" adjacent code encountered in these files. Naming a file below grants permission only to touch the named functions/members/regions plus the mechanical necessities (includes, forward declarations, enum/case entries, vcxproj membership) the named deletion requires.

### In scope

**Engine deletions**

- `Engine/Source/Network/NetworkProtocol.h` — `kClientSpawnRequest` enumerator (`:20`), `ClientRequestFlags`/`ClientRequestFlags_t` (`:37-42`), `PacketTypeName` case (`:52`), `GetClientPacketContract` case (`:165`).
- `Engine/Source/Network/NetworkMessages.h` — `ClientSpawnRequestMessage` (`:359-372`). No other message struct is touched.
- `Engine/Source/Network/Server/ServerTypes.h` — `PendingSpawnRequest` (`:29-33`). Sibling structs in the same header are untouched.
- `Engine/Source/Network/Client/Client.h` — `SendSpawnRequest` declaration (`:90`); `Engine/Source/Network/Client/ClientSend.cpp` — its definition (`:64-81`).
- `Engine/Source/Network/Server/Server.h` — `mPendingSpawnRequests` member (`:181`), `ClientSpawnRequest` declaration (`:210`); `Engine/Source/Network/Server/ServerReceive.cpp` — `Server::ClientSpawnRequest` (`:116-140`); `Engine/Source/Network/Server/Server.cpp` — dispatch case (`:280-281`) and the per-connection clear (`:80`); `Engine/Source/Network/Server/ServerSessionRuntime.cpp` — the reset clear (`:121`).

**Game deletions**

- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerClientManager.h` — `ProcessSpawnRequests` declaration (`:21`).
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerClientManager.cpp` — `ProcessSpawnRequests` definition (`:35-59`), plus the `QueueSpawnForClient` comment that references it by name (`:19`); reword that comment to state the revive behavior without the dangling cross-reference. `QueueSpawnForClient`'s behavior is unchanged.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — the `mpClientManager->ProcessSpawnRequests();` call in `ServerSession::AfterNetworkPoll` (`:293`). The other calls in that function stay and keep their order.

**Wire version and documentation**

- `Engine/Source/Network/NetworkProtocol.h` — `kuiProtocolVersion` (`:71`) bumped past 7; see Coordination for the shared-bump rule.
- `Documents/Architecture/Network.md` — remove the `kClientSpawnRequest` contract row (`:65`) and update the "current wire-protocol version is 7" note (`:78`) to the new version.

### Out of scope

- Any replacement packet. No `GamePacketType` enumerator is added, and no `GetGamePacketContract` row is added. The relocation option is rejected in Context; do not reintroduce it.
- Spawn/respawn *policy*. `QueueSpawnForClient`, `mClientsWaitingForSpawn`, `mDeadClientIds`, `mProcessedClientIds`, `NewClients`, and `FinalizeNewClients` keep their current behavior — the only change is that the always-empty second entry point disappears.
- The live fleet spawn packets `kClientSpawnIntoFleetRequest` / `kClientRespawnInFleetRequest` and their handlers, contract rows, and `HudScreen.cpp` callers.
- Reordering, renaming, or renumbering any surviving `PacketType` or `GamePacketType` enumerator beyond the shift the deletion mechanically causes.
- Game message wire-format pairing (`kServerAssignPlayer`, `kServerPlayerState`, `kServerFleetSync`) — completed separately; byte-identical there, no interaction with this bump.
- The `StatusChange` batch codec and its version gate — `Documents/Plans/Network/StatusChangeWireVersionGate.md`.

## Risk tier and invariants

**Tier 3.** Trigger: incompatible wire/protocol change — an engine packet type is retired and the resulting enumerator shift renumbers the engine tail and the whole game packet space. Invariants to hold:

- **Protocol version**: the renumber requires a `kuiProtocolVersion` bump beyond 7 unless atomically co-landed with another incompatible wire change behind one new version. The handshake version check is what makes the renumber safe against an old peer.
- **Relative order preserved**: no surviving enumerator is reordered. Every remaining engine packet type keeps its position relative to the others, and `GamePacketType` keeps its full order anchored to `kGamePacketStart`.
- **Wire-only exposure**: `PacketType` / `GamePacketType` bytes appear only in network code (`Engine/Source/Network/**`, `Projects/.../Source/Network/**`, plus the `Game.cpp` dispatch). Confirm no persisted artifact — save, replay, `.pack` — stores a packet type byte, so the renumber cannot invalidate stored data.
- No CRC / determinism / `Frame::kiVersion` exposure — this is transport, not simulation state.

## Acceptance criteria

- No `engine::` symbol names spawn or respawn: `kClientSpawnRequest`, `ClientRequestFlags`(`_t`), `PendingSpawnRequest`, `ClientSpawnRequestMessage`, `Client::SendSpawnRequest`, `Server::ClientSpawnRequest`, and `Server::mPendingSpawnRequests` are gone, and a repo-wide search for those identifiers returns only plan documents.
- `ServerClientManager::ProcessSpawnRequests` is gone — declaration, definition, and call site — and no empty stand-in replaces it. `QueueSpawnForClient` and the waiting/dead/processed sets are byte-for-byte unchanged apart from the reworded comment.
- Client and server both build; no dangling reference to the deleted types remains in either project.
- `kuiProtocolVersion` is greater than 7, and `Documents/Architecture/Network.md` shows the new version with the `kClientSpawnRequest` row removed.
- Runtime: connect a client, spawn into a fleet from the HUD, die, and respawn in fleet — spawn and respawn still work end to end through the fleet packets. A client built at the old protocol version is rejected at handshake rather than misparsing renumbered packet bytes.

## Coordination

- Protocol/version batch with the completed FleetGuid request re-key: these wire breaks may share one new `kuiProtocolVersion` bump only when atomically co-landed; otherwise each incompatible release bumps beyond the current version 7. (The unsubscribe-epoch wire change already consumed version 7 independently.)
- `Documents/Plans/Network/StatusChangeWireVersionGate.md` decides when a wire-layout change must bump `kuiProtocolVersion`; whichever of the two lands second follows the rule the first established.

## Notes

- Deletion is not cheaper than relocation on the wire — both renumber and both bump. It is cheaper everywhere else: no new enumerator, contract row, queue, send wrapper, or decode branch, and one more dead function removed.
- The completed server spawn-rate-bound change named `ProcessSpawnRequests` in its out-of-scope boundary. That is a boundary statement, not a dependency, and needs no action here.
