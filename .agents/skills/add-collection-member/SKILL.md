---
name: add-collection-member
description: >-
  Add a Structure-of-Arrays member pointer to an engine or game Collection without breaking allocation, persistence, serialization, CRC, transfer, hydration, or identity behavior. Use when adding a field, member, or data column to a collection, and proactively whenever an implementation adds a `* __restrict` pointer to a Collection struct. Follow the complete layout-change checklist even when the request names only the declaration.
allowed-tools: [Read, Edit]
---

# Add a Collection Member

Treat every new SOA pointer as a layout change. Read the applicable Frame and Collections `AGENTS.md` files, the target collection header and implementation, its paired collection, and all producers/consumers before editing. Do not infer behavior from the type name.

## Choose the live variant

Preserve the target's existing accessor and lifecycle shape; do not normalize it to another variant.

| Variant | Live exemplar | Inspect for |
|---|---|---|
| Entirely shared game pair | [`Targets.h`](/Projects/BrokenEngineSandbox/Source/Frame/Collections/Targets/Targets.h), `TargetsInterpolate::Sync`, `TargetsPostRender::Add` | `SharedMembers()` with `Members()` forwarding to it; paired versions, copy, owner-fed sync, initialization, ID map |
| Shared/client-split game pair | [`Blasters.h`](/Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/Blasters.h), [`Blasters.cpp`](/Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/Blasters.cpp), `BlastersInterpolate::Update` | `SharedMembers()` plus guarded `ClientMembers()`, `Members()` composition, spawn, copy, `ClientInit`, transfer send |
| Server-visible engine pair | [`Pushers.h`](/Engine/Source/Frame/Collections/Pushers/Pushers.h), `PushersInterpolate::Sync`, `PushersPostRender::Add`, [`Frame.cpp`](/Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp) `Frame::kiVersion` | Existing `Members()`-only shape, per-struct versions, owner sync, zero-init, difference logging, ID map |
| Owner-synchronized client-only pair | [`Sounds.h`](/Engine/Source/Frame/Collections/Sounds/Sounds.h), `SoundsInterpolate::Sync`, `SoundsPostRender::Add` | Whole-file `BT_CLIENT`, `Members()` only, full carry-forward copy, `SyncData`/`Sync`, Add defaults, no shared version or differences |
| Controller-driven, fire-and-forget client-only pair | [`Puffs.h`](/Engine/Source/Frame/Collections/Puffs/Puffs.h), `PuffsInterpolate::Update`, `PuffsPostRender::AddControlled` | Whole-file `BT_CLIENT`, `Members()` only, selective controller copy, unconditional animated stores, complete Add initialization |

Stop and report a stale exemplar if it no longer demonstrates the claimed variant.

## Layout and version

1. Declare the pointer beside related columns, initialized to `nullptr`. Keep client-only columns and their accessor entries under the same narrow `BT_CLIENT` guard.
2. Insert it once, in semantic wire/layout order, into the accessor the target already uses:
   - Existing split collection: shared column in `SharedMembers()`; client-only column in `ClientMembers()`; preserve `Members()` composition.
   - Existing `Members()`-only collection: add it to `Members()`; do not introduce `SharedMembers()` merely because the collection is server-visible.
   - Preserve C-array-of-pointer placement as one tuple entry; the collection helpers visit its elements.
3. Confirm `SharedMembers()` remains a subset of `Members()`. The tuple drives allocation, growth, swap/remove, build-local read/write, and normally shared CRC/read; omission corrupts layout.
4. Bump the changed struct's `kiVersion` for every game collection and server-visible engine collection. Confirm every current engine `kiVersion` declaration and the `Frame::kiVersion` sum before finalizing; each live version-bearing struct contributes its term, so bumping the struct changes the sum. Pure client-only engine collections such as Sounds and Puffs have no version term and do not change persisted shared layout.

There is no separate server-write path to update. Frame broadcast uses the same `CollectionWrite(..., cols.Members())` walk as the save format; on server builds, a split collection's `Members()` must equal `SharedMembers()`. `SharedCollectionRead()` allocates/zeros full client storage and reads the shared tuple.

## CRC and differences

Decide CRC and diagnostic membership independently.

- A shared member normally reaches `SharedCollectionCrc()` through existing `SharedMembers()` or `Members()`.
- If the collection has `SharedCrcMembers()`, explicitly include or exclude the member and preserve the subset relation. This is a determinism contract; if intended membership is unresolved, classify the decision Tier 3 and stop for user direction.
- Decide separately whether `LogDifferences()` should compare the member. Its coverage may intentionally differ from CRC membership. Follow the collection's live diagnostic intent; use `common::LogDifference<"name">` for scalar-like values and `common::LogDifference_Vec` for vectors. Client-only collections may have no difference logger.

## Persistence and producers

Trace how every row receives and retains the value.

1. Keep allocation automatic through the tuple. In `AllocateAndCopy()`, copy only state that must carry forward there. Some Update-produced columns are loaded from the previous frame and stored into current storage instead; some transition-only/controller/identity columns are copied. Follow the target's actual pattern.
2. For an Update-owned column, load previous state and store current state on every iteration. Keep the store unconditional and after branches/early-out decisions unless the collection's documented transition-only copy path owns persistence.
3. For owner-fed state, add the field to `SyncData`, assign it in `Sync()`, and update every `Sync()` aggregate initializer/caller. Preserve the exemplar's carry-forward mechanism: Targets copies before owner sync, Pushers copies in Update, and Sounds copies in `AllocateAndCopy()`.

## Creation, transfer, and hydration

1. Initialize every new slot in the collection's real creation API: game `Spawn`, engine `Add`, or controller `AddControlled`. Update `SpawnInfo` and every caller when the value is supplied externally. Initialize both sides of paired storage as applicable; never rely on freshly grown memory.
2. If the value must survive cross-cell transfer, update the complete path:
   - `TransferData` declaration and its member tuple when applicable;
   - source `TransferRequest` construction;
   - transfer wire serialization/deserialization and payload-size accounting when the field crosses the network;
   - [`SpawnTransfer.cpp`](/Projects/BrokenEngineSandbox/Source/SpawnTransfer.cpp) receive mapping, destination `SpawnInfo`, and spawn assignment.
   Use Blasters for the live send/receive shape. Match existing client guards; do not invent a second receive path.
3. For a client-owned handle/resource, initialize or create it in per-row `ClientInit`, preserve `ClientInitAll` full-state hydration through [`ClientSessionReceive.cpp`](/Projects/BrokenEngineSandbox/Source/Network/Client/ClientSessionReceive.cpp), initialize it on local spawn, copy it only where ownership persists, and update teardown/removal.

## Identity and agent queries

- For `CollectionFlags::kIdToIndex`, tuple membership makes swap-and-pop move the column automatically. If the new value changes identity/key semantics, update map construction, lookup, removal, and comparisons; otherwise make no ID-map edit.
- Decide whether a server-visible game field belongs in the deliberately minimum-and-cheap agent result. Current exposure lives in [`AgentCommandsServerQueries.cpp`](/Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsServerQueries.cpp) through `ExtractPlayers`, `ExtractSpaceships`, `ExtractMissiles`, `ExtractBlasters`, and `ExtractTargets`. Record an intentional exclusion when no scenario needs it.

## Completion checklist

- [ ] Pointer and correct tuple position; split/accessor shape preserved
- [ ] Version bump and `Frame::kiVersion` contribution verified when applicable
- [ ] CRC membership decided; difference logging decided independently
- [ ] Allocation/copy or unconditional Update persistence complete
- [ ] Spawn/Add/controller initialization and all parameter callers complete
- [ ] Owner `SyncData`/`Sync`/callers complete when applicable
- [ ] Transfer send, wire, receive, and destination initialization complete when applicable
- [ ] Client hydration, local creation, persistence, and teardown complete when applicable
- [ ] Identity semantics and agent-query exposure explicitly decided
- [ ] Paired cardinality, tuple subset/order, and client/server guards remain valid

`DestroyElement`, `SwapElement`, growth, serialization, and allocation need no member-specific calls after correct tuple placement. `extern template` declarations and explicit collection instantiations also do not change for a member-only edit.

## Framework references

- [`Collection.h`](/Engine/Source/Frame/Collections/Collection.h) — shared CRC/read, serialization, ID helpers
- [`CollectionMemory.h`](/Engine/Source/Frame/Collections/CollectionMemory.h) — tuple-driven allocation, growth, swap, destroy
- [Engine Collections instructions](/Engine/Source/Frame/Collections/AGENTS.md)
- [Game Collections instructions](/Projects/BrokenEngineSandbox/Source/Frame/Collections/AGENTS.md)
