---
name: add-collection-member
description: >-
  Add a Structure-of-Arrays member pointer to an engine or game Collection without breaking allocation, persistence, serialization, CRC, transfer, hydration, or identity behavior. Use when adding a field, member, or data column to a collection, and proactively whenever an implementation adds a `* __restrict` pointer to a Collection struct. Follow the complete layout-change checklist even when the request names only the declaration.
allowed-tools: [Read, Edit, Bash, PowerShell]
---

# Add a Collection Member

Treat every new SOA pointer as a layout change. Read the applicable Frame and Collections `AGENTS.md` files, the target collection header and implementation, its paired collection, and all producers/consumers before editing. Do not infer behavior from the type name.

## Choose the live variant

Preserve the target's existing accessor and lifecycle shape; do not normalize it to another variant.

| Variant | Live exemplar | Inspect for |
|---|---|---|
| Entirely shared game pair | `/Projects/BrokenEngineSandbox/Source/Frame/Collections/Targets/Targets.h`, `TargetsInterpolate::Sync`, `TargetsPostRender::Add` | `SharedMembers()` with `Members()` forwarding to it; paired versions, copy, owner-fed sync, initialization, ID map |
| Shared/client-split game pair | `/Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/Blasters.h`, `/Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/Blasters.cpp`, `BlastersInterpolate::Update` | `SharedMembers()` plus guarded `ClientMembers()`, `Members()` composition, spawn, copy, `ClientInit`, transfer send |
| Server-visible engine pair | `/Engine/Source/Frame/Collections/Pushers/Pushers.h`, `PushersInterpolate::Sync`, `PushersPostRender::Add`, `/Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp` `Frame::kiVersion` | Existing `Members()`-only shape, per-struct versions, owner sync, zero-init, difference logging, ID map |
| Owner-synchronized client-only pair | `/Engine/Source/Frame/Collections/Sounds/Sounds.h`, `SoundsInterpolate::Sync`, `SoundsPostRender::Add` | Whole-file `BT_CLIENT`, `Members()` only, full carry-forward copy, `SyncData`/`Sync`, Add defaults, no shared version or differences |
| Controller-driven, fire-and-forget client-only pair | `/Engine/Source/Frame/Collections/Puffs/Puffs.h`, `PuffsInterpolate::Update`, `PuffsPostRender::AddControlled` | Whole-file `BT_CLIENT`, `Members()` only, selective controller copy, unconditional animated stores, complete Add initialization |

Stop and report a stale exemplar if it no longer demonstrates the claimed variant.

## Layout and version

Initialize the pointer to `nullptr` beside related columns, and keep a client-only column and its accessor entry under the same narrow `BT_CLIENT` guard.

Wire/layout order is semantic, so the tuple position is yours to judge. Insert the column once, into the accessor the target already uses: do not introduce `SharedMembers()` merely because the collection is server-visible, and keep a C-array of pointers as one tuple entry, since the collection helpers visit its elements. The tuple drives allocation, growth, swap/remove, build-local read/write, and normally shared CRC/read; omission corrupts layout. The collection-layout auditor (see below) settles the subset and guard relations.

Version bumps apply to every game collection and server-visible engine collection. Each live version-bearing struct contributes its term, so bumping the struct changes the sum. Pure client-only engine collections such as Sounds and Puffs have no version term and do not change persisted shared layout.

There is no separate server-write path to update. Frame broadcast uses the same `CollectionWrite(..., cols.Members())` walk as the save format; on server builds, a split collection's `Members()` must equal `SharedMembers()`. `SharedCollectionRead()` allocates/zeros full client storage and reads the shared tuple.

## CRC and differences

A shared member normally reaches `SharedCollectionCrc()` through existing `SharedMembers()` or `Members()`. A collection with `SharedCrcMembers()` needs an explicit include-or-exclude call that preserves the subset relation: this is part of the rules that keep the simulation bit-identical across client and server, so if intended membership is unresolved, classify the decision Tier 3 (see root AGENTS.md, Risk tiers) and stop for user direction.

`LogDifferences()` coverage may intentionally differ from CRC membership, so follow the collection's live diagnostic intent; use `common::LogDifference<"name">` for scalar-like values and `common::LogDifference_Vec` for vectors. Client-only collections may have no difference logger.

## Persistence and producers

Trace how every row receives and retains the value; allocation itself stays automatic through the tuple.

Which mechanism carries the value forward is the judgment call, and it differs per collection: `AllocateAndCopy()` copies only state that must carry forward there, while an Update-owned column is instead loaded from the previous frame and stored into current storage on every iteration, with the store unconditional and after branches and early-out decisions unless a documented transition-only copy path owns persistence. Some transition-only, controller, and identity columns are copied instead. Owner-fed state travels through `SyncData` and `Sync()`, and every `Sync()` aggregate initializer and caller has to agree. Follow the target's live pattern: Targets copies before owner sync, Pushers copies in Update, and Sounds copies in `AllocateAndCopy()`.

## Creation, transfer, and hydration

Freshly grown memory holds nothing, so every new slot is initialized in the collection's real creation API — game `Spawn`, engine `Add`, or controller `AddControlled` — on both sides of paired storage, with `SpawnInfo` and its callers extended whenever the value is supplied externally.

Whether the value must survive cross-cell transfer is your decision; if it must, the whole path moves together: the `TransferData` declaration and its member tuple when applicable, source `TransferRequest` construction, wire serialization/deserialization and payload-size accounting when the field crosses the network, and `/Projects/BrokenEngineSandbox/Source/SpawnTransfer.cpp` receive mapping plus destination `SpawnInfo` and spawn assignment. Use Blasters for the live send/receive shape. Match existing client guards; do not invent a second receive path.

A client-owned handle or resource is created in per-row `ClientInit` and must also survive `ClientInitAll` full-state hydration through `/Projects/BrokenEngineSandbox/Source/Network/Client/ClientSessionReceive.cpp`, local spawn, and teardown/removal; copy it only where ownership persists.

## Identity and agent queries

- For `CollectionFlags::kIdToIndex`, tuple membership makes swap-and-pop move the column automatically. If the new value changes identity/key semantics, update map construction, lookup, removal, and comparisons; otherwise make no ID-map edit.
- Decide whether a server-visible game field belongs in the deliberately minimum-and-cheap agent result. Current exposure lives in `/Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsServerQueries.cpp` through `ExtractPlayers`, `ExtractSpaceships`, `ExtractMissiles`, `ExtractBlasters`, and `ExtractTargets`. Record an intentional exclusion when no scenario needs it.

## Completion checklist

- [ ] Pointer declared in the intended tuple position; the split/accessor shape
  the collection already used is preserved
- [ ] Version bumped on the changed struct when applicable
- [ ] CRC membership decided; difference logging decided independently
- [ ] Allocation/copy or unconditional Update persistence complete
- [ ] Spawn/Add/controller initialization and all parameter callers complete
- [ ] Owner `SyncData`/`Sync`/callers complete when applicable
- [ ] Transfer send, wire, receive, and destination initialization complete when applicable
- [ ] Client hydration, local creation, persistence, and teardown complete when applicable
- [ ] Identity semantics and agent-query exposure explicitly decided
- [ ] Paired element counts remain valid and tuple order still reads as the
  intended wire order
- [ ] Collection-layout auditor run and clean (below)

`DestroyElement`, `SwapElement`, growth, serialization, and allocation need no member-specific calls after correct tuple placement. `extern template` declarations and explicit collection instantiations also do not change for a member-only edit.

## Collection-layout auditor

Run from the repository root:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .agents/scripts/Test-CollectionLayout.ps1
```

Sweeps, shell-specific invocation, exit codes, truncation, JSON shape, and the blocking rule: `../../references/collection-layout-auditor.md`.

## Framework references

- `/Engine/Source/Frame/Collections/Collection.h` — shared CRC/read, serialization, ID helpers
- `/Engine/Source/Frame/Collections/CollectionMemory.h` — tuple-driven allocation, growth, swap, destroy
- Engine Collections instructions: `/Engine/Source/Frame/Collections/AGENTS.md`
- Game Collections instructions: `/Projects/BrokenEngineSandbox/Source/Frame/Collections/AGENTS.md`
