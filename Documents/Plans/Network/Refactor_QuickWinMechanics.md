# Refactor: Network Quick-Win Mechanics

## Context

Source: /external-refactor-clean on `Engine/Source/Network` (recursive). Six independent single-file mechanical fixes, each verified against code; none changes wire format or determinism.

## Design

### Engine/Source/Network/Client/ClientSessionBase.cpp
- `BuildSubscriptionQueue`'s fixed `GridCoord activeCoords[16]` (`:171`) is written with no bound check (`activeCoords[iActiveCount++]`, `:177`); the game passes `kiDesiredCoordSlots = 16` so it is exactly full at worst case today, while the engine supports `NetworkManager::kiMaxEnetCoordSlots = 64` — raising the game constant by one silently overflows the stack (`Client/CLAUDE.md` documents the ceiling as "not asserted"). Size the array to `NetworkManager::kiMaxEnetCoordSlots` (512 B stack) or `ASSERT` the bound before the write. [~5m]
- `ConnectToServer` passes `serverAddress.data()` (a `string_view`, not guaranteed null-terminated) to `Client`'s `const char*` ctor → `enet_address_set_host` (`ClientSessionBase.cpp:19`, `Client.cpp:33`). Current callers are safe by accident (string literal; null-terminated `char[16]`). Copy to a bounded local (`snprintf("%.*s")`) before passing. [~5m]

### Engine/Source/Network/NetworkProtocol.h (+ NetworkDiscoveryScanner.cpp)
- `inline constexpr int64_t kiDiscoveryScanMs = 1500` (`NetworkProtocol.h:80`) is only consumed as `std::chrono::milliseconds(kiDiscoveryScanMs)` (`NetworkDiscoveryScanner.cpp:70`, type-safe today). Make it chrono-typed at the source per style rule 12: `inline constexpr std::chrono::milliseconds kDiscoveryScanDuration = 1500ms;`. [~5m]

### Engine/Source/Network/Server/ServerSessionBase.{h,cpp}
- `SendNewSubscriptionFullStates` declares `[[maybe_unused]] int64_t iTick` (`ServerSessionBase.cpp:49`, decl `ServerSessionBase.h:33`) and never uses it — the body reads `game::gpGame->TickCounter()` (`:68`). Drop the parameter; the sole caller is `ServerSession::SubscriptionUpdates(iTick)` (`ServerSession.cpp:411`), whose own `[[maybe_unused]] int64_t iTick` (`:409`) then loses its last use — drop it there too and adjust the `BroadcastTick` call site. [~10m]

### Engine/Source/Network/Client/ClientReceive.cpp
- `Client::ServerCoordFullState` runs the full `DecompressAndReadFrame` (LZ4 decompress + `game::Frame` construction + `ServerRead`) at `:159` **before** the `uiSlotIndex >= std::ssize(mCoordSlots)` validity check at `:166` — a bad slot index pays the whole cost then discards. Hoist the slot check above the decompress (the cursor reads at `:149-152` are already done and cheap). [~5m]

### Engine/Source/Network/Client/Client.cpp (or Poll entry points)
- Add a one-line debug thread-affinity assert at the `Poll` entry points (`Client::Poll`, `Server::Poll`) making the area's verified main-thread-only contract checkable — the sim statics and `SendPacket`'s workbuffer use would corrupt silently if a poll ever moved off-thread. Idiom: `ASSERT(common::gpMultithreading->IsMainThread())` — the helper already exists (`Common/Threading/Multithreading.h:20`, currently uncalled), and invariant `ASSERT`s are established in the area (`ClientSessionBase.cpp:239`). This is an invariant assert, not parameter validation — consistent with the trust-boundaries-only rule. (Doc half lives in `Architecture_SpecDocReconciliation.md`.) [~10m]

## Critical files

- `Engine/Source/Network/Client/ClientSessionBase.cpp`, `ClientReceive.cpp`, `Client.cpp`
- `Engine/Source/Network/Server/ServerSessionBase.{h,cpp}`, `Server.cpp`
- `Engine/Source/Network/NetworkProtocol.h`, `NetworkDiscoveryScanner.cpp`
- Game caller of `SendNewSubscriptionFullStates` (grep at execution)

## Out of scope

- `aiResendTicks` removal (`Engine/DeadCodeAndUnusedIncludesSweep.md` item 5).
- The gate-only `pClient` fetches in `ServerReceive.cpp:131-135,155-161` — intentional trust-boundary validation; cosmetic inline not worth a change.
- `ClientGuidHash` combiner hardening (`NetworkProtocol.h:96-101`) — maps hold dozens of entries; no practical collision risk (REFUTED as a bug at review).
- `TrackReceivedTick` gap-overflow disconnect (`Client.cpp:248-251`) — verified the flag-based teardown does reach the server within a frame via the game session + `~Client` (`enet_peer_disconnect` + drain); leave as is.
- `mSubscriptionQueue.erase(begin())` O(n²) — ≤16 entries rebuilt per frame; not worth touching.
- `ComputeClockCorrectionNs` decomposition — fold into a future touch of that function, not standalone.

## Acceptance criteria

- Client + server build clean; behavior identical (the assert and bound are debug-only; the hoisted check only reorders validation before work).

## Notes

- No CRC/determinism/wire exposure. The `activeCoords` and `iTick` items touch `ClientSessionBase.cpp`/`ServerSessionBase.{h,cpp}` also edited by `Architecture_IncludeHygiene.md` and `Architecture_ClientSessionBoundary.md` — co-schedule or refresh lines (see Order.md File Groups).

## Verification Notes

All six items verified against source (2026-06-10); two enriched in place:
- `activeCoords[16]` unbounded write confirmed (`ClientSessionBase.cpp:171,:177`); `Client/CLAUDE.md` documents the ceiling as a fixed 16-entry stack array "not asserted", as cited.
- `serverAddress.data()` hazard confirmed (`ClientSessionBase.cpp:19` → `Client.cpp:33` `enet_address_set_host(const char*)`); both current callers are safe by accident as claimed — `"127.0.0.1"` literal (`Game.cpp:868`) and null-terminated `mcDiscoveredAddress` char[16] (`ClientSession.cpp:240`).
- `kiDiscoveryScanMs` confirmed at `NetworkProtocol.h:80` with the sole consumer at `NetworkDiscoveryScanner.cpp:70`; style rule 12 (chrono types + literals) verified in `C++StyleGuide.txt`.
- Dead `iTick` confirmed (`ServerSessionBase.cpp:49`, `ServerSessionBase.h:33`); game caller chain identified and written into the item (`SubscriptionUpdates` `ServerSession.cpp:409-411`).
- Hoist confirmed: `DecompressAndReadFrame` (LZ4 + `game::Frame` construction + `ServerRead`) at `ClientReceive.cpp:159` precedes the slot check at `:166`; the cursor reads at `:149-152` have no dependency on the moved check.
- Thread-affinity assert: idiomatic helper exists (`common::Multithreading::IsMainThread()`, `Multithreading.h:20`) though currently uncalled — this would be the first thread-affinity assert in the codebase; invariant-assert precedent exists (`ASSERT(false)` `ClientSessionBase.cpp:239`, `ASSERT(...)` in `GameBase::RenderFrame`). Not a trust-boundary violation. Idiom written into the item.
