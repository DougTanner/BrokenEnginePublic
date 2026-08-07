<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-07T12:56:20.155Z","dependsOn":[]} -->
# Recover server update ticks discarded on adoption overflow

## Context

/external-deep-analysis (2026-08-07) of `Projects/BrokenEngineSandbox/Source/Network/Client` found, and Phase-3 verification confirmed from re-derived source evidence, that the client can acknowledge a delta tick it never retains:

- Engine receive appends each update and calls `TrackReceivedTick` before game adoption (`Engine/Source/Network/Client/ClientReceive.cpp:347`); contiguous arrivals advance the ACK floor (`Engine/Source/Network/Client/Client.cpp:323,342`).
- Game adoption then drops an update once the per-coord `serverUpdates` map holds 256 entries (`Projects/BrokenEngineSandbox/Source/Network/Client/ClientSessionReceive.cpp:163`), and `PollAndDrain` sends the already-advanced ACK afterward (`Engine/Source/Network/Client/ClientSessionRuntime.cpp:220`).
- The server resends only unset ACK gaps (`Engine/Source/Network/Server/ServerSend.cpp:207`, `Documents/Architecture/Network.md` "Server"/Resend), so an acked-but-discarded tick is never resent. No overflow path requests a full state; resync requests originate only in desync recovery (`ClientDesyncManager.cpp:111,145`), and ring eviction removes the resend source without initiating recovery.

Reachable sequence: a slow client or server burst delivering 257+ contiguous ticks for one coord in one drain window. Result: reconciliation waits permanently at the missing tick until an unrelated explicit resync or disconnect.

## Design

Keep engine ACK tracking and wire behavior unchanged. Make the 256-entry overflow in `ApplyReceivedUpdates` atomically enter the existing full-state resync path for that coord (the same request desync recovery already sends), so every acknowledged tick either adopts or triggers guaranteed authoritative recovery. This stays inside game-side adoption code, outside deterministic frame execution, and reorders nothing in CRC simulation.

Decided against the alternative (making ACK tracking contingent on adoption): it would reach into Engine-owned ACK floor/bit bookkeeping (`Client.cpp:323,342`) for the same observable outcome the existing resync path already provides.

Acceptance invariant: every acknowledged delta tick is adopted or authoritatively recovered; no permanent gap.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSessionReceive.cpp`

## In scope

- The `ApplyReceivedUpdates` overflow path only: detect the discarded-tick case and trigger the existing full-state resync request for the affected coord

## Out of scope

- Wire format, ACK packet layout, engine ACK floor/bit tracking (`Engine/Source/Network/Client/Client.cpp`, `ClientReceive.cpp`), and server resend policy (all stay as documented)
- Reconciliation ring/replay logic (separate plan `Documents/Plans/Network/ReconcileFullStateInjectionRingBase.md`)
- Any change to deterministic simulation or CRC state

## Risk tier and invariants

Change Workflow Tier 2 — scoped client adoption behavior using the existing resync request; no wire, ACK, determinism, or CRC change. The decided design stays entirely in `ClientSessionReceive.cpp`; if implementation proves engine receive or ACK tracking must change after all, that is the independently-owned cross-subsystem Tier-3 trigger and the plan escalates. Invariants: ACK stream semantics per `Documents/Architecture/Network.md` (acked = retained or recovered); receive drain order (static → full → delta) unchanged; no deterministic-state exposure (ACK timing is send-only, not CRC'd).

## Acceptance criteria

- A harness scenario (or targeted log-verified run) delivering an over-budget contiguous burst for one coord shows the discarded ticks recovered via full-state resync, with reconciliation advancing past the former gap.
- Steady-state behavior with normal buffers is unchanged (no extra resyncs, no ACK regressions).
