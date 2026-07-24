<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Stabilize Frame Update Pipeline Reference

## Context

`Documents/Architecture/FrameUpdatePipeline.md` (237 lines) is the maintained architecture reference for frame ownership and phase ordering, but it currently also mirrors volatile implementation details: individual helper calls, branch predicates, timing variables, and explicit per-collection inventories. The architecture-documentation review that produced this follow-up found two stale save-path labels (`Quickload` and `Quicksave`) even though save behavior was outside its scope; those specific labels have since been corrected (the document now names `GameSaveLoad::SaveLoadReplay()` and `GameSaveLoad::TickAutosave()`), but the structural exposure that let them go stale remains.

That exposure is the problem this plan fixes: routine implementation changes can invalidate diagram labels that are not architectural contracts. The user decision is to keep this reference focused on stable ownership and phase sequencing, with direct code links for details. This is a documentation-only rewrite of one file; no runtime behavior changes.

## Design

Rewrite `Documents/Architecture/FrameUpdatePipeline.md` in place so that each of its five existing sections communicates only durable boundaries, applying these rules uniformly:

1. **Keep** for every stage: its owner (which function/class runs it), the order between stages, and the client/server or simulation/render affinity callers rely on. Examples of durable content: the five sequential `RunFrameTick()` phases (Interpolate, PostRender, Collision, Transfer, Destroy/Spawn); that both `GameBase::ServerUpdate()` and client reconciliation call the same `RunFrameTick()`; the client main-loop order ProcessInput → ClientUpdate → Render → AudioManager::Update; the server dual-buffer `pCurrent`/`pNext` swap versus the client snapshot-ring lifecycle.
2. **Remove** leaf-call inventories, transient branch predicates (e.g. `IsStalled?`, `Swapchain deferred or frame poisoned?`, `iFullTicks > 0?`, `Final replay reader retired?`), timing variables (`iFullTicks`, `miTickCounter`, `mfCurrentTime`), queue/resend/subscription mechanics, and the explicit collection name lists in "Collection Phase Participation" — unless the item expresses a documented ordering or ownership invariant, in which case keep it and state that invariant in prose.
3. **Link**: beside each section, add relative Markdown links to the canonical source entry points or registration headers that own its implementation details (the files in Critical files below). Every link must resolve to a tracked file from the document's location (`Documents/Architecture/`, so links use `../../` prefixes).
4. **State the maintenance boundary** explicitly in the document (extend the existing blockquote at the top or an equivalent short section): update this document when ownership, phase order, lifecycle, or affinity changes; within-stage implementation details live in the linked code.
5. **Verify against code** while simplifying: read the listed source entry points and confirm every retained ownership, ordering, and affinity claim matches current code, so reducing detail does not erase or misstate a real architectural constraint. Do not add new mirrored detail while verifying.

Keep the document's existing five-section structure and its Mermaid diagram form; simplify diagram contents rather than replacing the document's shape.

## Critical files

In-scope (the only file edited):

- `Documents/Architecture/FrameUpdatePipeline.md` — all five sections: "RunFrameTick Pipeline", "Client Main Loop", "Server Main Loop", "Frame Lifecycle", "Collection Phase Participation", plus the maintenance-boundary blockquote at the top.

Read-only verification sources (used to confirm stable ownership and phase order; never edited, and their volatile inner helpers, packet drains, queue mechanics, and branch predicates are removed from the document rather than mirrored):

- `Projects/BrokenEngineSandbox/Source/Frame/FrameTick.cpp` — `RunFrameTick()` simulation phase ordering.
- `Engine/Source/Main.cpp` and `Engine/Source/GameBase.cpp` — client/server main-loop ownership and sequencing (`GameBase::ClientUpdate()`, `GameBase::ServerUpdate()`).
- `Engine/Source/Network/Client/ClientSessionRuntime.cpp` and `Engine/Source/Network/Server/ServerSessionRuntime.cpp` — stable engine-owned network cycle boundaries and enforced phase order.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp` and `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — game-policy façades composed into those cycles.
- `Engine/Source/Frame/FrameBase.h`, `Projects/BrokenEngineSandbox/Source/Frame/Frame.h`, and `Projects/BrokenEngineSandbox/Source/Frame/FrameCollections.h` — frame lifecycle and collection ownership; `ForEachInterpolateUpdate`/`ForEachPostRender` phase dispatch lives in `Engine/Source/Frame/FrameUtils.h` and is a valid link target for the collection-participation section.

## Scope contract

This listed scope is both target and ceiling. In scope: editing `Documents/Architecture/FrameUpdatePipeline.md` only, within the five named sections and top blockquote, applying exactly the five design rules above. The implementer makes the smallest complete change and adds no abstractions, configuration, tooling, refactors, or fixes to adjacent code or documents it encounters; naming a verification source file grants no permission to edit it.

Out of scope:

- Changing runtime code, phase ordering, collection registration, client/server behavior, or save/replay behavior — including anything noticed in the read-only verification sources.
- Reworking `Documents/Architecture/GameReconciliation.md` or `Documents/Architecture/Network.md`, or any AGENTS.md.
- Generating diagrams from source or adding documentation tooling.
- Adding implementation walkthroughs that duplicate the linked source.

## Risk tier

Tier 1 — mechanical, documentation-only. No public signature or invariant exposure; no build or agent-harness run is required. No determinism/CRC, wire protocol, serialization/layout, replay, client/server guard, threading, allocation-tracked, shader, or data-pack behavior may change (nothing outside `Documents/Architecture/FrameUpdatePipeline.md` is written).

## Acceptance criteria

- Every retained diagram communicates stable ownership, phase ordering, lifecycle, or affinity without depending on incidental helper-call structure.
- Volatile leaf calls, branch predicates, timing variables, and explicit collection inventories are removed unless the document identifies the architectural invariant they represent.
- Each section links to the canonical source files that carry its implementation details, and every relative link resolves to a tracked file.
- The Mermaid diagrams remain syntactically valid and agree with the current source entry points and phase order (`RunFrameTick()` five-phase sequence, `GameBase::ClientUpdate()`/`GameBase::ServerUpdate()` sequencing, server dual-buffer vs client snapshot-ring lifecycle).
- The document states the maintenance boundary: architecture document updates only for ownership, phase-order, lifecycle, or affinity changes.
- A fresh documentation coherence review finds no stale symbol, ownership, or sequencing claim.

## Notes

- Queue scoring (historical anchor, not a scheduler input): Effort 1, Impact 2, Risks 0, Score -1 (`Quick Win`).
