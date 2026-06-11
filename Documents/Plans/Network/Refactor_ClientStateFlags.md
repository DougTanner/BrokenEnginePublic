# Refactor: Network Bool Members to Flags

## Context

Source: /external-refactor-clean on `Engine/Source/Network` (recursive). Root CLAUDE.md mandates `common::Flags<EnumType>` over multiple `bool` members; the Network client classes accumulated three clusters. The same files already use `common::Flags` extensively for the `Classify*` results, so the pattern is established locally.

## Design

### Engine/Source/Network/Client/Client.h
- Six `bool` members (`mbConnected`, `mbConnectionAccepted`, `mbDisconnectedEvent`, `mbHasLastUpdateArrival`, `mbDesyncDebugMode`, `mbLoadNotificationReceived` — `Client.h:182-184,217,220-221`) → one `common::Flags<ClientStateFlags>` with named accessors preserving the existing getter surface (`IsConnected()`, `WasDisconnected()`, …). [~45m]

### Engine/Source/Network/Client/ClientSessionBase.h
- Four `bool` members (`ClientSessionBase.h:44-45,55,60`) → `common::Flags<SessionStateFlags>` (or fold into the same enum if lifetimes align — grill). [~20m]

### Engine/Source/Network/Server/ServerTypes.h
- `ClientCoordSubscription::bActive` + `bFirstUpdateLogged` (`ServerTypes.h:9-10`) → `common::Flags<SubscriptionFlags>`. [~15m]

## Critical files

- `Engine/Source/Network/Client/Client.h`, `Client.cpp`, `ClientSend.cpp`, `ClientReceive.cpp`
- `Engine/Source/Network/Client/ClientSessionBase.{h,cpp}`
- `Engine/Source/Network/Server/ServerTypes.h`, `Server.cpp`, `ServerReceive.cpp`, `ServerSend.cpp`

## Out of scope

- The `Classify*` flag enums (`Client.h:149-174`) — already `common::Flags`, working as designed.
- Any state-machine behavior change — pure representation swap, compile-checked.
- `CoordSubscriptionState` (a proper enum, not bools) — untouched.

## Acceptance criteria

- No multi-`bool` clusters remain in the three classes; all call sites compile; connection/subscription behavior identical in an interop playtest.

## Notes

- No wire/CRC exposure (none of these bools are serialized — verified: only protocol structs cross the wire). Mechanical but wide-touch: schedule alongside another plan already touching `Client.h` (e.g. `Architecture_ClientSessionBoundary.md`) rather than standalone — see Order.md File Groups.

## Verification Notes

All items verified against source (2026-06-10):
- Counts and lines exact: `Client.h` six bools (`mbConnected`/`mbConnectionAccepted`/`mbDisconnectedEvent` `:182-184`, `mbHasLastUpdateArrival` `:217`, `mbDesyncDebugMode` `:220`, `mbLoadNotificationReceived` `:221`); `ClientSessionBase.h` four (`mbServerDiscovered`/`mbDiscoveryScanTimedOut` `:44-45`, `mbClockErrorDisconnect` `:55`, `mbNoFreeSlotLogged` `:60`); `ServerTypes.h` two (`bActive`/`bFirstUpdateLogged` `:9-10`).
- Not-serialized claim verified: every wire write in `ClientSend.cpp`/`ServerSend.cpp`/`ServerReceive.cpp` is a field-by-field `PushBack`/`Read*` of protocol values — no struct `memcpy` touches any of these members. `ClientCoordSubscription` is server-internal only.
- `= {}` aggregate resets of `ClientCoordSubscription` (`Server.h:83` `FreeSlot`) and `ClientCoordSlot` survive a Flags conversion unchanged.
- Observation (not added — separate cluster, lower value): `ClientConnection` carries two more bools (`Server.h:36` `bHandshakeComplete`, `:53` `bFloorStalled`), non-adjacent and interleaved with counters; left out of this plan's three clusters deliberately rather than expanded.
