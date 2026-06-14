# `/Engine/Source/Network/Server/` - Server Networking

## Overview

Server side of engine networking: the listening ENet host, per-client `ClientConnection` records, the ring buffers behind the ACK/resend protocol, and the tick-timing session base game sessions inherit. Game policy (fleets, spawn rules, authorized coords, broadcast scheduling) lives in the game layer — this directory is transport plus the drain queues the game layer consumes each tick.

## Key Classes

- **Server** (`gpServer`) - ENet host owning connection lifecycle and two ring buffers: a per-coord LZ4-compressed delta ring for unreliable update/resend, and an uncompressed full-frame ring serving reliable debug-frame requests (compressed lazily at request time). One persistent compression scratch buffer is reused across all frames/clients — safe because everything here runs on the server main thread. One class split across `Server.cpp` / `ServerReceive.cpp` / `ServerSend.cpp` by concern; `ServerTypes.h` holds the PODs game headers also need.
- **ServerSessionBase** - Fixed-rate tick timing: high-resolution waitable timer sleeps until ~2ms before target, then spins to precision (overshoot tracked, logged only under `kbProfilingFrameSpike`). Owns `NetworkDiscoveryResponder`; `PollNetworkBase` services both host and responder.

## Build Configs

Every file here is `BT_SERVER`-wrapped and server-vcxproj-only: `ServerSessionBase.*` plus `Server.{h,cpp}`, `ServerTypes.h`, `ServerReceive.cpp`, and `ServerSend.cpp`. Only the server build constructs `Server` (`Main.cpp`, inside the `#else` of the `BT_CLIENT` split); no client TU references `Server`/`ClientConnection`/`gpServer` — `Server.h` is reached only by the three Server TUs, the server-only `ServerDisplay.cpp`, and `Engine.h`'s `BT_SERVER` span, and `ServerTypes.h` is reached only through `Server.h`. Each `.cpp`/`.h` carries a whole-file `#if defined(BT_SERVER)` guard **and** is excluded from the client vcxproj (guards alone would leave empty translation units compiling in the client). The `game::gpServerSession->SendTimespeedToNewClient` call in `ClientHello` is therefore plain code, not a `#if defined(BT_SERVER)` island.

## Invariants

- **Handshake gate**: `ClientHello` validates protocol version and `game::Frame::kiVersion` (mismatch rejects + disconnects), warns on build-config mismatch, and mints a `ClientGuid` (`UuidCreate`) only when the client sent an empty one. Later handlers look clients up via `FindHandshakenClient` (unsubscribe excepted — it only frees a slot).
- **Drain queues**: the per-poll drains here are spawn requests, disconnects (client id + GUID so the game layer can persist fleet state), new subscriptions, resync client ids, and opaque game packets.
- **Subscribe adjacency**: 3x3 of any authorized coord; `kOriginCoord` always allowed (initial fleet spawn). Authorized list maintained by game layer. Rejected/no-free-slot subscribes reply with sentinel slot `0xFF`.
- **Slot reuse**: `FreeSlot` preserves the epoch; the next subscribe on the slot increments it — the server half of the hub's epoch-drop model.
- **ACK floor** clamped to latest buffered tick (a confused client cannot push the floor past what was sent, which would disable resends); client timestamp echo is monotonic-guarded. Floor-stall tracking is per connection, not per slot: 3 consecutive ACK packets advancing no slot mark it stalled (peak tracked for resolve logging).
- **Ring contiguity**: `BufferFrame` must run exactly once per tick per active coord with monotonically increasing ticks — resend lookup indexes by tick offset from the ring front and breaks silently on gaps. `ClearBufferedFrames` resets both rings around save-load so restarted tick numbers cannot alias stale entries.
- **Ring pruning**: per-coord rings drop immediately when a coord leaves the active set (its deltas are dead; a resubscriber needs full state anyway); both rings cap at `kiMaxBufferedFrames`.
- **Resend preconditions**: a slot is scanned only if its bitfield has at least one ACK above the floor (all-zero means latency, not a gap); resends cap at `kiMaxResendFrames` per slot per tick; the current tick always goes via `SendUpdate`.
- **Resend logging is delta-only**: state transitions only, with per-slot cooldown — steady-state packet loss must not spam. New per-slot logging should follow the same pattern.
- **New-subscription race guard**: full state is sent post-tick (`SendNewSubscriptionFullStates`), revalidating client/slot/coord first — an unsubscribe or disconnect in the same poll batch may have freed the slot.
- **Resync**: `ClientResyncRequest` queues the client id for the game layer to re-send full states; `BroadcastLoadNotification` tells all handshaken clients to drop state and resync after a server-side save-load.
- **Network simulation**: incoming packets route through a delayed-packet queue keyed by release time; fast-forward (`miTimeMultiply > 1`) flushes the queue immediately, and disconnect purges that peer's pending packets (a delayed packet for a freed peer would be a use-after-free).

## See Also

- Parent: [../CLAUDE.md](../CLAUDE.md)
- Game session: [Projects/BrokenEngineSandbox/Source/Network/Server/CLAUDE.md](../../../../Projects/BrokenEngineSandbox/Source/Network/Server/CLAUDE.md)
- [Network.md](../../../../Documents/Architecture/Network.md)
