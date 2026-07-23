# Cleanup: Complete Superseded Drain Contract Plan

## Context

Engine-owned session runtimes already absorb the receive-buffer access and servicing work this historical plan was intended to restructure. `ClientSessionRuntime` and `ServerSessionRuntime` expose the current engine-side receive buffers to the game-policy façades, and those façades perform the required adoption and servicing. No runtime or code implementation remains for this plan.

## Design

Retain this unmarked tombstone as a manual historical reference. Its intended runtime work was completed elsewhere, so it must not participate in scheduler selection or receipt-bound terminal processing.

## Critical files

- `Documents/Plans/Network/Refactor_DrainContractUnification.md` — unmarked historical tombstone.
- `Engine/Source/Network/Client/ClientSessionRuntime.cpp` and `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp` — current client runtime/facade ownership evidence.
- `Engine/Source/Network/Server/ServerSessionRuntime.cpp` and `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — current server runtime/facade ownership evidence.

## Out of scope

- Any runtime, protocol, serialization, receive-buffer-lifetime, reset, or gameplay behavior change.
- Renaming or refactoring receive-buffer APIs.
- Reintroducing executable metadata without a newly approved implementation scope.

## Acceptance criteria

- This tombstone lacks byte-zero executable metadata and is omitted by WorktreeCli scheduler selection.
- The document accurately records that no runtime work remains.

## Coordination

No live scheduler dependency remains. `Refactor_ClientResetUnification.md` is independently retained as a manual tombstone.

## Notes

Historical documentation only. No runtime, determinism/CRC, protocol, serialization, replay, affinity, threading, trust-boundary, or tracked-allocation invariant is exposed.
