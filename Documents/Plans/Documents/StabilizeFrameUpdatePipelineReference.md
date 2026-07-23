<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Stabilize Frame Update Pipeline Reference

## Context

`Documents/Architecture/FrameUpdatePipeline.md` is intended to explain frame ownership and phase ordering, but its current 237-line form also mirrors volatile implementation details: individual helper calls, branch predicates, timing variables, and explicit collection inventories. The architecture-documentation review that produced this follow-up found two stale save-path descriptions (`Quickload` and `Quicksave`) even though save behavior was outside its scope.

The resulting maintenance burden weakens the document as an architecture reference: routine implementation changes can invalidate labels that are not architectural contracts. The user decision is to keep this reference focused on stable ownership and phase sequencing, with direct code links for details.

## Design

- Recast each diagram and its surrounding prose around durable boundaries: the owner of each stage, the order between stages, and the client/server or simulation/render affinity that callers rely on.
- Remove leaf-call inventories, transient branch predicates, counters, and collection name lists unless they express a documented ordering or ownership invariant.
- Add relative Markdown links beside each section to the canonical source entry points or registration headers that own its implementation details.
- State the maintenance boundary explicitly: update the architecture document when ownership, phase order, lifecycle, or affinity changes; leave within-stage implementation details to linked code.
- Compare the simplified diagrams against current code so reducing detail does not erase a real architectural constraint.

## Critical files

- `Documents/Architecture/FrameUpdatePipeline.md` — architecture reference to simplify.
- `Projects/BrokenEngineSandbox/Source/Frame/FrameTick.cpp` — simulation phase ordering.
- `Engine/Source/Main.cpp` and `Engine/Source/GameBase.cpp` — client/server main-loop ownership and sequencing.
- `Engine/Source/Network/Client/ClientSessionRuntime.cpp` and `Engine/Source/Network/Server/ServerSessionRuntime.cpp` — stable engine-owned network cycle boundaries and enforced phase order.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp` and `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — game-policy façades composed into those cycles.
- `Engine/Source/Frame/FrameBase.h`, `Projects/BrokenEngineSandbox/Source/Frame/Frame.h`, and `Projects/BrokenEngineSandbox/Source/Frame/FrameCollections.h` — frame lifecycle and collection ownership references.

Use the runtime/façade entry points only to verify stable ownership and phase order. Remove volatile inner networking helpers, packet drains, queue mechanics, and branch predicates from the architecture document rather than mirroring them.

## Out of scope

- Changing runtime code, phase ordering, collection registration, client/server behavior, or save/replay behavior.
- Reworking `Documents/Architecture/GameReconciliation.md` or `Documents/Architecture/Network.md`.
- Generating diagrams from source or adding documentation tooling.
- Adding implementation walkthroughs that duplicate the linked source.

## Acceptance criteria

- Every retained diagram communicates stable ownership, phase ordering, lifecycle, or affinity without depending on incidental helper-call structure.
- Volatile leaf calls, branch predicates, timing variables, and explicit collection inventories are removed unless the document identifies the architectural invariant they represent.
- Each section links to the canonical source files that carry its implementation details, and every relative link resolves to a tracked file.
- The Mermaid diagrams remain syntactically valid and agree with the current source entry points and phase order.
- A fresh documentation coherence review finds no stale symbol, ownership, or sequencing claim.

## Notes

- Tier 1 documentation-only change. No build or agent-harness run is required.
- No determinism/CRC, wire protocol, serialization/layout, replay, client/server guard, threading, allocation-tracked, shader, or data-pack behavior may change.
- Queue scoring: Effort 1, Impact 2, Risks 0, Score -1 (`Quick Win`).
