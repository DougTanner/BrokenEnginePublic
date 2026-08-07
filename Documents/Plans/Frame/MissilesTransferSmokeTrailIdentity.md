<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-06T21:59:14.935Z","dependsOn":[]} -->
# Reuse client smoke-trail identity across authoritative missile transfer

## Context

`MissilesPostRender::Transfer` (`Frame/Collections/Missiles/Missiles.cpp:364`) records the smoke-trail identity only on `BT_CLIENT`, but the applied payload is server-authored (client harvesting disabled; server publishes transfers, client applies the received `StatusChange` — `Game.cpp:385`, `ServerBroadcaster.cpp:136`, `ReconcileReplayTick.cpp:128`). The server explicitly serializes identity 0 (`Network/NetworkSerialization.cpp:35`), so `MissilesInterpolate::ClientInit` allocates a new trail (`Missiles.cpp:332`, `Engine/Source/Frame/Collections/SmokeTrails/SmokeTrailsUpdate.cpp:41`) instead of reusing the live one. This violates `Missiles/AGENTS.md` ("Live transfer reuses smoke-trail identity"). Verified by /external-deep-analysis Phase-3 review (2026-08-06); pre-existing debt.

## Design

The smoke-trail id is client-local (allocated by the client visual counter), so it cannot travel through the server payload — and no server-side key can either: missile `TransferRequest`s do not populate `iEntityId`, `ServerTransferManager::CollectTransfers` drops request metadata when creating the `StatusChange`, and the missile wire payload carries only `TransferData`. The correlation is therefore purely client-local and content-based: under `BT_CLIENT`, when the source-cell simulation marks a live missile for transfer, record the destination coordinate, the missile's `TransferData` serialized in server-canonical form — with the client-only smoke-trail identity field zeroed, exactly as the server emits it (`NetworkSerialization.cpp:50-53`) — and the live smoke-trail identity alongside; when an authoritative live-missile arrival applies at that destination, look up a record whose server-canonical bytes are identical to the arrival payload (the client's deterministic simulation produces the same remaining bytes the server serialized) and consume it before `SpawnTransfer` so `ClientInit` reuses the identity. Byte-identical candidates are interchangeable — their trails are visually identical — so consuming any one match is correct. Unmatched arrivals (source not observed by this client) keep the current new-trail path. Records are removed when consumed, when their missile is destroyed, or once the arrival tick for their transfer has passed without a match. No shared, CRC, or wire field is added; falling arrivals keep creating no client effects.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/Missiles.cpp`, `Missiles.h`

## In scope

- The client-only outgoing-transfer record and its arrival-side consumption around `MissilesPostRender::Transfer`, the transfer-arrival spawn path, and `MissilesInterpolate::ClientInit`.
- Cleanup of stale records (missile destroyed, arrival never observed).

## Out of scope

- Any shared/CRC/wire state; server code paths; blaster and player transfer scalars (owned by `Documents/Plans/Frame/BlastersPlayersTransferVisualState.md`); engine SmokeTrails collection.

## Risk tier and invariants

Change Workflow Tier 2 — client-only visual behavior in one subsystem; no determinism/CRC, wire, or layout surface. Invariants: shared deterministic state untouched; no heap allocation added in the main loop (use existing client containers or workbuffer per `Engine/Source/Memory/AGENTS.md`).

## Acceptance criteria

- /agent-harness scenario: a live missile crossing between two client-visible cells keeps one continuous smoke trail; a falling arrival still creates no client effects.
- Client and server build clean through `/compile`; per-tick CRC unchanged.
