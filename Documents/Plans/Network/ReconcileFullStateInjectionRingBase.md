<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-07T12:56:17.864Z","dependsOn":[]} -->
# Fix committed ring head after due full-state injection

## Context

/external-deep-analysis (2026-08-07) of `Projects/BrokenEngineSandbox/Source/Network/Client` found, and Phase-3 verification confirmed from re-derived source evidence, that a due pending full-state injection can leave the committed snapshot-ring head pointing at the old rollback slot while `iConfirmedTick` names the authoritative tick:

- `ReconcileReplay.cpp:22` — `ReconcileInjectPendingFullState` stores the authoritative frame in a new physical slot and resets the replay stack/write head, but nothing retains that slot as the output-ring base.
- Matching-tick injection happens before CRC validation (`ReconcileReplayTick.cpp:252`); validation records index zero and advances the confirmed tick (`ReconcileReplayTick.cpp:215`). When that is the final validated tick, the index-zero branch selects the old rollback slot as `outputLayout.iHead` (`ReconcileReplayTick.cpp:284`), and writeback pairs that head with the newer confirmed tick (`ReconcileReplay.cpp:45`).
- Confirmed-tick injection has the sibling defect: injection at `ReconcileReplay.cpp:220`, but the no-further-validation layout reconstructs from the rollback slot at `ReconcileReplay.cpp:287`.

Reachable sequence: contiguous server updates through the due full-state tick, with that tick equal to the replay endpoint. Result: ring metadata names the authoritative tick while the committed head frame is an older speculative frame — timeline regression, false desync, or attempted re-simulation of frozen state. This violates the authoritative-base contract in `Documents/Architecture/GameReconciliation.md` ("Pending Full State Injection") and the frozen-confirmed-state invariant in `Projects/BrokenEngineSandbox/Source/Network/Client/AGENTS.md` ("Reconciliation Invariants").

## Design

Bookkeeping-only correction: after any due full-state injection (rollback-base or matching-tick), retain the injected physical slot as the logical output-ring base until a later validated frame supersedes it. Both downstream layout paths must derive from that slot:

- the index-zero/final-validated-tick head selection in `ReconcileReplayCoord` (`ReconcileReplayTick.cpp:284` region), and
- the no-further-validation `ComputeOutputLayout` branch (`ReconcileReplay.cpp:287` region).

Determinism constraint (mandatory): the fix changes ring metadata/base selection only. It must not reorder per-tick simulation, `StatusChange` application, CRC computation, or any floating-point evaluation; the committed frame bytes for any tick that was already correct must be byte-identical before and after the fix.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplay.cpp`
- `Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplayTick.cpp`
- `Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplay.h` (only if the injected-slot handoff needs a new field on the existing replay work state)

## In scope

- `ReconcileInjectPendingFullState` and the injected-slot bookkeeping it feeds
- `ReconcileReplayCoord`'s validated-index-to-head selection
- The no-further-validation output-layout reconstruction in `ReconcileReplay.cpp`

## Out of scope

- Any change to tick simulation, transfer/`StatusChange` ordering, CRC field order, or replay-range selection
- Fast-path CRC processing (`ReconcileReplayCrc.cpp`)
- The ACK/adoption handoff (separate plan `Documents/Plans/Network/ClientAckAdoptionOverflowGap.md`)

## Risk tier and invariants

Change Workflow Tier 3 — determinism/CRC-adjacent reconciliation state. Invariants: server-validated ticks stay frozen (`Client/AGENTS.md` Reconciliation Invariants); replay/catch-up stay within the ring budget; PostRender state remains bit-deterministic and CRC-checked.

## Acceptance criteria

- After a due full-state matching-tick injection at the replay endpoint, the committed ring head is the frame named by `iConfirmedTick` (observable via the existing reconcile logging or a harness scenario driving a full-state resync with simulated loss).
- After confirmed-tick injection with no further validated replay tick, writeback commits the injected slot, not the rollback slot.
- Replay determinism check via `/agent-harness` passes; no new desyncs in the smallest loss/resync scenario.
