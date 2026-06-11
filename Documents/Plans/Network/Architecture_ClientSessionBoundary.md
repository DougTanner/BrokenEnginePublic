# Architecture: Client/Session Boundary Narrowing and Manager Vestiges

## Context

Source: /external-architecture-review on `Engine/Source/Network` (recursive). Three cohesion findings around the `Client` ↔ `ClientSessionBase` seam and the `NetworkManager` singleton: GUID disk persistence living inside packet send/receive handlers, the session mutating the client's slot internals through expose-everything getters, and a write-only `gpNetworkManager` global.

## Design

### Relocate GUID persistence out of `Client` (deep-module move)
- `Client` currently owns GUID **disk** persistence: load in `Client::SendHello` (`ClientSend.cpp:163-185`) and atomic write inside the `Client::ServerConnectionResponse` packet handler (`ClientReceive.cpp:405-421` via `gpFileManager`) — the only `gpFileManager` cross-manager reference in the directory, and a session-lifecycle concern living in the transport class. Move load/store to `ClientSessionBase` (load before connect, store via a small callback/accessor when the server assigns a GUID); `Client` keeps only the in-memory `ClientGuid`. [~45m]

### Narrow the slot-mutation seam
- `ClientSessionBase` cancels an in-flight subscription by directly zeroing the slot through non-const `GetCoordSlots()` (`Client.h:126`, mutation at `ClientSessionBase.cpp:149`) and pushing onto `GetCancelledSubscriptions()` (`:150`) — slot-lifecycle invariants co-owned across the boundary. Add an intent-named `Client::CancelSubscription(int64_t iSlot)` that performs the zero + cancelled-list push as one operation; demote the two getters toward const/private as call sites allow. Note the second non-const consumer: the game's server-load reset force-zeroes all slots and clears the cancelled list through the same getters (`ClientSession.cpp:360-365`) — give it its own named operation (e.g. `Client::ResetAllSlots()`) or the getters stay non-const. [~30m]

### `gpNetworkManager` vestige
- `gpNetworkManager` is assigned (`NetworkManager.cpp:10,19`) and never read anywhere in the repo; all useful surface is `static`. Either delete the global (instance stays, as RAII for `enet_initialize`/`deinitialize`) or add a one-line comment declaring it lifetime-only — grep-verify zero readers at execution. [~10m]

## Critical files

- `Engine/Source/Network/Client/Client.h`, `ClientSend.cpp`, `ClientReceive.cpp`
- `Engine/Source/Network/Client/ClientSessionBase.{h,cpp}`
- `Engine/Source/Network/NetworkManager.{h,cpp}`

## Out of scope

- The dual access paths to `Client`/`Server` (`gpClient` global vs `mpClientNetwork` member; `gpServer` vs `MainThread` local) — both mechanisms are sanctioned engine patterns; not churned.
- The subscription FSM's cross-file slicing (`ClientSend`/`ClientReceive`/`ClientSessionBase` transition split) — the `Classify*` choke points are working as designed; full FSM consolidation is not worth the churn now.
- Any wire-format or handshake behavior change — the GUID bytes sent/received are unchanged; only where the file I/O lives moves.

## Acceptance criteria

- `Client` no longer references `gpFileManager`; GUID file load/store happens in `ClientSessionBase`; reconnect still presents the persisted GUID (same file path/format).
- Slot cancellation goes through one named `Client` method; no non-const slot container handed across the boundary for that flow.

## Notes

- **Invariant exposure**: none on the wire — the GUID file format, path, and handshake bytes are unchanged; client-only code paths. Allocation discipline: the existing file-I/O suppression wrappers move with the code.

## Verification Notes

Verified against source (2026-06-10); two corrections applied:
- GUID load confirmed at `ClientSend.cpp:166-185` (inside `SendHello`); atomic write confirmed at `ClientReceive.cpp:405-421` — but the handler is `Client::ServerConnectionResponse`, not "ServerHello" (corrected above). `gpFileManager` appears nowhere else in `Engine/Source/Network`. Note the timing constraint for the move: `SendHello` fires from the `ENET_EVENT_TYPE_CONNECT` branch inside `Client::Poll` (`Client.cpp:99`), so the session-side load must complete before/at `Client` construction or be handed in via accessor before the connect event — the plan's "load before connect" shape is correct.
- Slot mutation confirmed at `ClientSessionBase.cpp:149-150` via non-const `GetCoordSlots()` (`Client.h:126`) + `GetCancelledSubscriptions()` (`:127`). Found one additional non-const consumer the plan missed — game `ClientSession.cpp:360-365` (server-load force-reset) — added above; `ProfileManager.cpp:239` uses the const overload and is unaffected.
- `gpNetworkManager` zero-readers re-grepped repo-wide: only the declaration (`NetworkManager.h:38`), the two assignments (`NetworkManager.cpp:10,19`), and docs/plans mention it. Confirmed write-only; all used surface is `static` as claimed.
