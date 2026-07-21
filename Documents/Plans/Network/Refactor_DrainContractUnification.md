# Cleanup: Complete Superseded Drain Contract Plan

## Context

Engine-owned session runtimes already absorb the receive-buffer access and servicing work this row was intended to restructure. `ClientSessionRuntime` and `ServerSessionRuntime` expose the current engine-side queues to the game-policy façades, and those façades perform the required adoption and servicing. No runtime or code implementation remains for this plan.

## Design

After this row's current live prerequisite lands and its WorktreeCli row is complete, remove this tombstone and complete the existing `Refactor_DrainContractUnification` row through WorktreeCli. WorktreeCli already encodes that dependency; deletion and row completion are the only executable actions.

## Critical files

- `Documents/Plans/Network/Refactor_DrainContractUnification.md` — cleanup tombstone removed when its row is completed.
- `Engine/Source/Network/Client/ClientSessionRuntime.cpp` and `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp` — current client runtime/facade ownership evidence.
- `Engine/Source/Network/Server/ServerSessionRuntime.cpp` and `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — current server runtime/facade ownership evidence.

## Out of scope

- Any runtime, protocol, serialization, queue-lifetime, reset, or gameplay behavior change.
- Renaming or refactoring receive-buffer APIs.
- Completing this row before its current live prerequisite lands and completes.

## Acceptance criteria

- This tombstone file is removed.
- The owner-held `Refactor_DrainContractUnification` WorktreeCli row is completed.
- WorktreeCli queue validation reports `ok: true` after completion.

## Coordination

Complete after the current live prerequisite encoded by WorktreeCli. `Refactor_ClientResetUnification` remains blocked until this row lands and completes.

## Notes

Tier 1 documentation and queue cleanup only. No runtime, determinism/CRC, protocol, serialization, replay, affinity, threading, trust-boundary, or tracked-allocation invariant is exposed. No builds or runtime verification are required.
