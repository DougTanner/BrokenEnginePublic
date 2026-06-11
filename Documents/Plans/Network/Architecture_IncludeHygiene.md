# Architecture: Network Include Hygiene

## Context

Source: /external-architecture-review on `Engine/Source/Network` (recursive). The area has zero std-header violations and no include cycles, but carries unused includes, one `.cpp` missing its own header (the sole deviation in the area), two headers that compile only via fragile `Engine.h` ordering beyond the documented aggregation convention, and `GridCoord` exposure identical to the case already tracked in `Frame/Architecture_IncludeHygiene.md`.

## Design

### Unused includes (grep-verified, remove)
- `Engine/Source/Network/Client/Client.cpp:8` — `Network/NetworkCursor.h`: no cursor symbol in the TU; also supplied by `Client.h:6`. [~5m]
- `Engine/Source/Network/Client/ClientSend.cpp:7` — `Network/NetworkCursor.h`: TU uses only `Workbuffer::PushBack`/`Append` and `SendSimplePacket` (body in `Client.h`, which includes the cursor). [~5m]
- `Engine/Source/Network/Client/ClientReceive.cpp:8` — `Game.h`: zero `Game.h`-declared symbols referenced; `game::Frame`/`game::StatusChange` arrive via the Pch (`Pch.h:96`). [~5m]

### Self-containment fixes (exceed even the documented Engine.h-ordering convention)
- `Engine/Source/Network/Server/ServerSessionBase.cpp` includes neither its own header nor any header whose symbols it uses (defines `ServerSessionBase::*`, calls `gpServer->*`, `mpDiscoveryResponder->Poll()`, uses `PendingNewSubscription`) — add `Network/Server/ServerSessionBase.h` at minimum (every other `.cpp` in the area includes its primary header). [~5m]
- `Engine/Source/Network/Server/ServerTypes.h:35` — `GridUpdateData::statusChanges` is `std::span<const game::StatusChange>` with no declaration in scope; it rides `NetworkSerialization.h:7`'s fwd-decl via Engine.h ordering, and `Server.h`'s own `game` fwd-decl block (`Server.h:13-19`) comes *after* `#include "ServerTypes.h"` (`Server.h:4`). Add `namespace game { struct StatusChange; }` to `ServerTypes.h`. [~5m]
- `Engine/Source/Network/Server/ServerSessionBase.h:15` — inline ctor calls `std::make_unique<NetworkDiscoveryResponder>()`, requiring the complete type, but the header only fwd-declares it (`:6`); works solely because `Engine.h:90` precedes `:92`. Either include `Network/NetworkDiscoveryResponder.h` or move the ctor body to the `.cpp` (prefer the latter — keeps the header light). [~10m]

### `BT_CLIENT`/`BT_SERVER` guard scope (narrow-guard rule)
- `Engine/Source/Network/Client/ClientSessionBase.cpp:3-7` — `#include "Game.h"` sits above the `#if defined(BT_CLIENT)`; siblings `Client.cpp`/`ClientReceive.cpp` put it inside. Move it inside the guard. [~5m]
- `Engine/Source/Network/Server/ServerSessionBase.cpp:1-4` — mirrored defect: `Game.h` above `#if defined(BT_SERVER)`. Move inside. [~5m]

### `GridCoord.h` direct includes (mirrors `Frame/Architecture_IncludeHygiene.md`)
- `GridCoord` is used by `NetworkCursor.h:67`, `ServerTypes.h:8`, `Client.h:35`, `ClientSessionBase.h:26`, `Server.h:37`, yet `Engine/Source/Frame/GridCoord.h` is included by none of them (it arrives from the *game's* `Frame/Frame.h:4` via `Pch.h:96`). Add the direct include to the five headers. [~10m]

### Include order/style
- `Engine/Source/Network/Server/ServerSend.cpp:3-6` — missing blank line separating the corresponding header from the engine group (style rule 47). `Server.cpp:5-6` group order resolves itself when the `MemoryManager.h` removal lands (see Out of scope). [~5m]

## Critical files

- `Engine/Source/Network/Client/Client.cpp`, `ClientSend.cpp`, `ClientReceive.cpp`, `ClientSessionBase.cpp`
- `Engine/Source/Network/Server/ServerSessionBase.{h,cpp}`, `ServerTypes.h`, `ServerSend.cpp`
- `Engine/Source/Network/NetworkCursor.h`, `Client/Client.h`, `Server/Server.h`

## Out of scope

- The 3× unused `Memory/MemoryManager.h` includes (`Server.cpp:6`, `ServerReceive.cpp:5`, `ServerSend.cpp:5`) — owned by `Engine/DeadCodeAndUnusedIncludesSweep.md` item 1 (same orphaned-by-the-`AllocationTracking.h`-move class).
- `Client.h`'s dead `game::player_t` alias — owned by the same sweep (item 4).
- Headers relying on documented `Engine.h` aggregation order for `NetworkManager`/`NetworkProtocol`/`NetworkSimulation` symbols (`Client.h`/`Server.h` using `DelayedPacket`, `AckState`, `SendPacket`) — conforms to the documented hub convention; not churned here.
- Redundant-but-used `NetworkCursor.h` includes in receive/send TUs that genuinely use cursor symbols — correct under the `Network/CLAUDE.md:11` include-where-used rule.

## Acceptance criteria

- Client and server projects build clean after each removal/addition; `ServerTypes.h` and `ServerSessionBase.h` no longer depend on sibling-header fwd-decls or aggregation ordering for the symbols named above.

## Notes

- Includes-only, compile-checked; no CRC/determinism/network-protocol exposure. If `Architecture_ClientBuildServerTransportGate.md` lands first, the `ServerSessionBase.cpp` guard-move item may shift line numbers — refresh citations.

## Verification Notes

All items verified against source (2026-06-10):
- `Client.cpp:8` `NetworkCursor.h`: zero cursor symbols in the TU (no `Read*`/`Write*`/`PushSimplePacketArg`); also transitively supplied by `Client.h:6`. Confirmed removable.
- `ClientSend.cpp:7` `NetworkCursor.h`: TU uses only `Workbuffer` members (`PushBack`/`Append`) and `SendSimplePacket` (template body in `Client.h`, which includes the cursor header itself at `:6` for `PushSimplePacketArg`). Confirmed removable.
- `ClientReceive.cpp:8` `Game.h`: no `gpGame` or other `Game.h`-declared symbol in the TU. `game::Frame` (complete, for `make_unique`/`ServerRead`) and `game::StatusChange` arrive via `Pch.h:96` → game `Frame/Frame.h`; `kiMaxStatusChangesPerCell` is `NetworkProtocol.h:75`; `DecompressStatusChangeBatch` is `NetworkSerialization.h`; `kiTickRate` is engine `Frame/TimeStep.h:6`. Confirmed removable.
- `ServerSessionBase.cpp` includes only `Pch.h` + `Game.h` (`:1-2`) — no self-include, the sole deviation in the area. Confirmed.
- `ServerTypes.h:35` `std::span<const game::StatusChange>` with no `game` declaration in the header; `Server.h`'s own `game` fwd-decl block (`Server.h:13-19`) indeed comes after `#include "ServerTypes.h"` (`Server.h:4`). Confirmed.
- `ServerSessionBase.h:15` inline ctor `std::make_unique<NetworkDiscoveryResponder>()` with only the `:6` fwd-decl; compiles solely because `Engine.h:90` includes `NetworkDiscoveryResponder.h` before `:92`. Confirmed.
- Guard scope: `ClientSessionBase.cpp` has `Game.h` at `:5` above `#if defined(BT_CLIENT)` at `:7`; `ServerSessionBase.cpp` has `Game.h` at `:2` above `#if defined(BT_SERVER)` at `:4`. Both confirmed (siblings put it inside).
- `GridCoord` uses confirmed at `NetworkCursor.h:67`, `ServerTypes.h:8`, `Client.h:35`, `ClientSessionBase.h:26`, `Server.h:37`; none of the five includes `Frame/GridCoord.h` (it arrives via `Pch.h:96` → game `Frame.h:4`). Confirmed.
- `ServerSend.cpp:3-6`: corresponding header at `:3` runs straight into the engine group with no blank line — style rule 47 confirmed. `Server.cpp:5-6` ordering (`Game.h` before `Memory/MemoryManager.h`) resolves with the sweep-owned MemoryManager removal, as stated.
- Existing-plan ownership confirmed: the 3× `MemoryManager.h` includes, `player_t` alias, and `aiResendTicks` are items 1/4/5 of `Engine/DeadCodeAndUnusedIncludesSweep.md` — correctly out of scope here.
