<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-25T16:04:48.760Z","dependsOn":[]} -->
# Island Placement Subscription Capacity

## Context

`engine::kiMaxPlacementsPerTemplate` is 1024, but that fixed per-template SSBO slab has no proof that it covers every allowed client state. The client contract permits 16 coordinate slots (`game::kiDesiredCoordSlots`: nine visible, six sticky, one spare). `ClientSessionRuntime::SynchronizeSubscriptions` keeps unwanted coordinates for `kStickySubscriptionDuration`, full states create confirmed coordinate frames, and `game::Game::ComputeActiveSet` renders every confirmed frame. `Islands::UpdateActiveIslands` emits every active cell's placements in its mesh-visible and offscreen passes.

`IslandChainPlacement` bounds one cell at 107 placements (six big islands plus six 16-slot Small surrounds and five tail Smalls). The current Small-role ceiling is 16 x 101 = 1616. Independently of current asset-bucket membership, allowing any one template to occupy every placement yields the conservative per-template safety bound 16 x 107 = 1712. At placement 1025, `EmitPlacement` asserts before it writes a neighbouring template's slab, and `common::Assert` throws. No overflow has been observed; this is a proven unbounded-by-current-capacity correctness risk, not a reported runtime failure.

This residual predates the approved comment-only SSBO-residency outcome. That completed work decided resident-memory scaling options and is not changed here; this Plan owns only the sticky-subscription capacity proof and correction.

## Design

1. Make the existing deterministic `IslandChainPlacement` worst-case-per-cell count a named header-level compile-time contract, derived from its existing Huge/Large/Medium, surround, and tail constants. Use that same value for the current placement-storage reserve so one source remains authoritative; do not change generation counts or placement order.
2. Raise `kiMaxPlacementsPerTemplate` from 1024 to the conservative per-template safety bound of 1712. Update its comment to name the 16-slot subscription contract and 107-placement-per-cell bound rather than claiming unproven headroom.
3. In `Islands.cpp`, add a compile-time assertion that the fixed slab capacity is at least `game::NetworkSessionContract::kiCoordSlots * kiMaxIslandPlacementsPerCell`. The existing game contract is already the canonical compile-time interface consumed by Engine code. This makes a future subscription-slot or placement-bound increase fail the client build until the SSBO capacity is explicitly reconsidered.

The correction keeps the fixed `templateIndex * kiMaxPlacementsPerTemplate` addressing, boot-baked indirect `firstInstance`, and record-once Global/Main command buffers. The four boot-allocated host-visible SSBO instances grow from 17,203,200 bytes (16.40625 MiB) to 28,761,600 bytes (27.4296875 MiB) at the captured 70-template set: +11,558,400 bytes (+11.0234375 MiB). No per-frame allocation, SSBO resizing, descriptor change, or command-buffer re-record is introduced.

## Critical files

- `Engine/Source/Frame/IslandChainPlacement.h` — expose the derived per-cell maximum as the placement generator's compile-time capacity contract.
- `Engine/Source/Frame/IslandChainPlacement.cpp` — consume that contract for the existing reserve/assert path without changing generated placements.
- `Engine/Source/Graphics/Islands.h` — set the 1712 fixed slab capacity and document its proven bound and resident-memory cost.
- `Engine/Source/Graphics/Islands.cpp` — assert the Graphics slab remains at least the product of the game session slot count and placement-generator maximum.

## In scope

- `Engine/Source/Frame/IslandChainPlacement.h` — add the named header-level compile-time per-cell placement maximum (`kiMaxIslandPlacementsPerCell`), derived from the existing Huge/Large/Medium, surround, and tail constants.
- `Engine/Source/Frame/IslandChainPlacement.cpp` — consume that constant for the existing placement-storage reserve/assert path; generation counts and placement order stay unchanged.
- `Engine/Source/Graphics/Islands.h` — raise `kiMaxPlacementsPerTemplate` from 1024 to 1712 and update its comment to name the 16-slot subscription contract, the 107-placement-per-cell bound, and the resident-memory cost.
- `Engine/Source/Graphics/Islands.cpp` — add the compile-time assertion that `kiMaxPlacementsPerTemplate >= game::NetworkSessionContract::kiCoordSlots * kiMaxIslandPlacementsPerCell`, next to the existing `EmitPlacement` capacity path.

## Out of scope

- Changing `kiDesiredCoordSlots`, sticky duration, confirmation/adoption, `mActiveCoords`, or any client/server wire or subscription behavior.
- Changing island generation rules, deterministic layout, CRC/replay behavior, `.pack` data, or asset bucket selection.
- Compact/dynamic SSBO arenas, active-template remapping, lower resident-memory targets, or any previously considered residency alternative.
- Replacing fixed SSBO slabs, indirect first-instance offsets, or record-once command buffers; dynamic recovery after an overflow is not a substitute for the compile-time bound.

## Risk tier and invariants

Tier 3 trigger: independently owned Network subscription, Frame placement, and Graphics fixed-buffer contracts meet in one capacity invariant. The runtime correction is client-only, but the compiler proof crosses those subsystem boundaries.

- The 16-slot client contract and 107-per-cell generator maximum remain the exact current inputs to the capacity proof.
- Server/client deterministic placement output is unchanged; only the client renderer's storage headroom changes.
- Fixed slab addressing and record-once command-buffer assumptions remain valid because every per-template slab grows uniformly at boot before recording.
- Allocation tracking remains clean in the render loop: the additional host-visible memory is allocated once during `Islands` construction and remains resident; no steady-state allocation is added.
- No serialization, `.pack`, `kiVersion`, replay, wire-format, or shader-layout change is permitted.

## Acceptance criteria

- The client build proves at compile time that `kiMaxPlacementsPerTemplate >= game::NetworkSessionContract::kiCoordSlots * kiMaxIslandPlacementsPerCell`; with current constants the values are 1712, 16, and 107 respectively.
- An existing `/agent-harness` normal connected-gameplay scenario reaches island-terrain rendering and reports a stable terrain screenshot/log state with no `Islands::EmitPlacement` overflow assertion or Vulkan validation error. This is regression smoke only; the compile-time assertion and build are the decisive extreme-bound proof, not a claim that the scenario populates 1712 placements.
- Existing terrain indirect rendering continues to use boot-fixed per-template `firstInstance` ranges and Global prepasses continue to use fixed full-slab draws; no command-buffer re-record or per-frame allocation occurs.
- Client target compiles; server deterministic placement behavior remains unchanged by source inspection and the shared target build.

## Notes

- No `dependsOn` edge or reciprocal `## Coordination` edit is required: the related live SSBO-residency Plan is terminally constrained to its separate comment-only resident-memory decision, while this Plan has a complete, independently executable capacity correction.
- Do not reduce this to a larger unexplained literal. The named placement maximum and cross-contract `static_assert` are required to keep the proof live when either input changes.
