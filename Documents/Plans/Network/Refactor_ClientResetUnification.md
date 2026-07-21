# Cleanup: Complete Superseded Client Reset Plan

## Context

The engine-owned client session runtime already absorbs the reset-state work this row was intended to unify. `ClientSessionRuntime` owns connection, clock, subscription, discovery, and transport reset mechanics; the game `ClientSession` façade owns gameplay-state reset hooks. No runtime or code implementation remains for this plan.

## Design

After `Refactor_DrainContractUnification` lands and its WorktreeCli row is complete, remove this tombstone and complete the existing `Refactor_ClientResetUnification` row through WorktreeCli. That deletion and row completion are the only executable actions.

## Critical files

- `Documents/Plans/Network/Refactor_ClientResetUnification.md` — cleanup tombstone removed when its row is completed.
- `Engine/Source/Network/Client/ClientSessionRuntime.cpp` — current engine-owned reset mechanics evidence.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp` — current game-policy reset hook evidence.

## Out of scope

- Any runtime, protocol, serialization, reset, reconciliation, desync, or gameplay behavior change.
- Moving, regrouping, or refactoring reset state.
- Completing this row before `Refactor_DrainContractUnification` lands and completes.

## Acceptance criteria

- This tombstone file is removed.
- The owner-held `Refactor_ClientResetUnification` WorktreeCli row is completed.
- WorktreeCli queue validation reports `ok: true` after completion.

## Coordination

Complete only after `Refactor_DrainContractUnification` lands and completes.

## Notes

Tier 1 documentation and queue cleanup only. No runtime, determinism/CRC, protocol, serialization, replay, affinity, threading, trust-boundary, or tracked-allocation invariant is exposed. No builds or runtime verification are required.
