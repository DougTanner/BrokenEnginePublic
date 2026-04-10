---
name: add-collection-member
description: >-
  Reference guide for adding new SOA member pointers to collection structs (engine or game). Use this skill when adding a field, member, or data column to any collection — e.g., "add speed to BlastersPostRender", "I need to track health per entity", "add a new array to the collection". ALSO use this skill proactively whenever your implementation plan requires adding a new `* __restrict` pointer to any Collection struct, even if the user didn't explicitly ask to "add a member" — the checklist must be followed any time a collection's memory layout changes. Every step in this checklist affects either compilation, memory layout, or deterministic CRC validation, so partial completion causes subtle bugs.
allowed-tools: [Read, Edit]
---

# Adding New Members to Collections

Checklist for adding a new SOA member pointer to a collection struct. Collections use Structure-of-Arrays layout where each "member" is a pointer to a contiguous array (e.g., `float* __restrict pfSpeed`).

Every applicable step must be completed — partial completion causes compilation errors, memory corruption, or determinism failures.

## Step 1: Declare the Pointer and Add to the Correct Members Method (Header)

Declare the pointer with the collection's other members, then add it to the appropriate tuple method.

**Which tuple method to use depends on whether the member is shared or client-only:**

- **Shared member** (needed by both client and server): Add to `SharedMembers()`
- **Client-only member** (visual/audio, guarded by `#ifdef BT_CLIENT`): Declare inside `#ifdef BT_CLIENT`, add to `ClientMembers()`
- **Collections without client/server split** (e.g., purely client-only collections like AreaLights): Add to `Members()` directly

Example — adding a shared member:
```cpp
float* __restrict pfSpeed = nullptr;  // New shared member
auto SharedMembers(this auto&& rSelf) { return std::tie(rSelf.pFlags, rSelf.pVecVelocities, rSelf.pfSpeed, rSelf.pAlignments); }
```

Example — adding a client-only member:
```cpp
#if defined(BT_CLIENT)
engine::sound_t* __restrict puiSounds = nullptr;  // New client-only member
#endif
// ...
#if defined(BT_CLIENT)
auto ClientMembers(this auto&& rSelf) { return std::tie(rSelf.puiSounds); }
#endif
```

**`SharedCrcMembers()` — CRC subset:** Some collections (e.g., Players) have a `SharedCrcMembers()` that is a subset of `SharedMembers()`, excluding fields like server-side bookkeeping (`pClientGuids`, `pGlobalPlayerIds`) or client-only animation state. If the collection has `SharedCrcMembers()`, decide whether the new member should participate in CRC validation and add it there too (or explicitly exclude it).

**`kiVersion` bump:** If the collection has a `static constexpr int64_t kiVersion`, bump it when adding a new shared member that changes the serialization layout.

**Why this matters:** `Members()` (which combines `SharedMembers()` + `ClientMembers()` via `std::tuple_cat`) drives all automatic operations:
- `CollectionCrc()` / `SharedCollectionCrc()` — deterministic CRC validation (Players overrides this with `SharedCrcMembers()` directly)
- `CollectionWrite()` / `CollectionRead()` — save file serialization
- `ServerCollectionRead()` — server stream deserialization (reads `SharedMembers()` only)
- `engine::Allocate()` / `engine::AllocateAndAssign()` — SOA buffer allocation
- `engine::GrowPairedCollections()` — capacity growth during spawn
- `engine::DestroyElement()` / `engine::SwapElement()` — swap-and-pop removal

If a member is in the tuple, all of the above handle it automatically. If it is missing, you get memory corruption.

## Step 2: Add to AllocateAndCopy (CPP)

`AllocateAndCopy()` copies frame-persistent state from the previous frame. Only add a `std::memcpy` for members that are NOT loaded/saved in the Update loop — typically owned object handles (IDs to child collections), identity fields (`puiIds`, `pAlignments`), and other "static" fields set once at spawn. Members fully handled by the Update load/save pattern (Step 5) do not need memcpy here. Check the existing `AllocateAndCopy` for the collection to see what it copies. The pattern is:

```cpp
void BlastersPostRender::AllocateAndCopy(BlastersPostRender& rCurrent, const BlastersPostRender& rPrevious)
{
    engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

    if (rCurrent.iCount > 0)
    {
        std::memcpy(rCurrent.pFlags, rPrevious.pFlags, rCurrent.iCount * sizeof(rCurrent.pFlags[0]));
        std::memcpy(rCurrent.pfSpeed, rPrevious.pfSpeed, rCurrent.iCount * sizeof(rCurrent.pfSpeed[0])); // New
    }
}
```

Client-only members go inside `#if defined(BT_CLIENT)` within the `if (rCurrent.iCount > 0)` block.

**Note:** `engine::Allocate()` uses `Members()` to size and position the buffer — that is handled by Step 1. But it does NOT copy data. `AllocateAndCopy()` selectively copies only the members that need frame-to-frame persistence.

## Step 3: Add to LogDifferences (CPP)

`LogDifferences()` logs per-field mismatches for desync diagnosis. Add a `common::LogDifference` call for the new member. Only **shared** (non-client-only) members are logged — client-only fields are excluded because only shared state participates in cross-build CRC validation.

```cpp
bool BlastersPostRender::LogDifferences(const BlastersPostRender& rOther) const
{
    common::ScopedLogDifferenceContext context("BlastersPostRender");
    bool bEqual = true;
    bEqual &= Collection::LogDifferences(rOther);

    for (int64_t i = 0; i < iCount; ++i)
    {
        bEqual &= common::LogDifference<"pFlags">(i, pFlags[i], rOther.pFlags[i]);
        bEqual &= common::LogDifference<"pfSpeed">(i, pfSpeed[i], rOther.pfSpeed[i]); // New
    }

    return bEqual;
}
```

Use `common::LogDifference<"fieldName">(index, value, otherValue)` for scalars/flags, and `common::LogDifference_Vec("fieldName", index, vec, otherVec)` for `XMVECTOR` members.

**Note:** LogDifferences logs ALL shared members for desync diagnosis, even those excluded from CRC via `SharedCrcMembers()`. Client-only engine collections (AreaLights, Sounds, etc.) do NOT have `LogDifferences()` at all — skip this step for those.

## Step 4: Initialize in Spawn (CPP) — Game Collections Only

Set a default value when spawning new elements. Game collections use `GrowPairedCollections()` + `AddElement()` then assign each member:

```cpp
void BlastersPostRender::Spawn(Frame& __restrict rFrame, const SpawnInfo& rInfo)
{
    // ... GrowPairedCollections + AddElement ...
    int64_t iIndex = engine::AddElement(rCurrentInterpolate, rCurrentPostRender);

    rCurrentPostRender.pFlags[iIndex] = rInfo.flags;
    rCurrentPostRender.pfSpeed[iIndex] = rInfo.fSpeed; // New
}
```

If the spawn uses a `SpawnInfo` struct, add the new field there too.

For engine collections with Add/Remove (e.g., AreaLights, Sounds, Pushers), initialize the member in the `Add()` method instead. **Sync-pattern collections** (where the owner calls `Sync()` each Interpolate phase) must zero-initialize all Interpolate fields in `Add()` — `Sync()` doesn't run until the next Interpolate phase, and CRC is computed after PostRender, so stale memory in the new Interpolate slot causes client/server desync.

## Step 5: Load/Process in Update (CPP) — If Applicable

If the member is read/written during the Update loop (loaded from previous frame, processed, saved to current frame), add the load and save lines:

```cpp
// Load from previous frame
float fSpeed = rPrevious.pfSpeed[i];

// ... processing ...

// Save to current frame
rCurrent.pfSpeed[i] = fSpeed;
```

**The load and save MUST be unconditional** — outside any early-exit branches (e.g., transfer lock, destroyed checks). If a conditional block skips the save, `rCurrent` will contain uninitialized memory for that field, causing determinism failures. Load at the top of the per-entity loop alongside other fields, save at the bottom.

Not all members need this — some are only set at spawn and copied via `AllocateAndCopy()`. Check the existing Update pattern for the collection.

## Step 6: Add to Transfer (CPP) — If the Collection Supports Transfers

If the collection has a `Transfer()` method that builds `TransferRequest`s for cross-cell entity migration, add the new member to the `TransferData` struct (or equivalent) so it survives the transfer:

```cpp
TransferRequest request
{
    .data = {
        .vecVelocity = rCurrentPostRender.pVecVelocities[i],
        .fSpeed = rCurrentPostRender.pfSpeed[i], // New
    },
};
```

## Step 7: Add to ClientInit (CPP) — Client-Only Owned Objects

If the new member is a client-only owned object handle (e.g., `engine::sound_t`, `engine::area_lights_t`, `engine::wind_trail_t`), add initialization in `ClientInit()` so the object is created when receiving server state (server streams exclude client-only fields):

```cpp
void BlastersInterpolate::ClientInit(Frame& rFrame, int64_t iIndex)
{
    // Initialize and Sync the new owned object...
}
```

## Summary Checklist

| Step | What | Where | When |
|------|------|-------|------|
| 1 | Add pointer + tuple method | Header (.h) | Always |
| 2 | Add memcpy | `AllocateAndCopy()` in .cpp | If value persists across frames |
| 3 | Add LogDifference | `LogDifferences()` in .cpp | Shared members on server-visible collections |
| 4 | Initialize at spawn | `Spawn()` or `Add()` in .cpp | Game collections / engine Add() collections |
| 5 | Load/process/save | `Update()` in .cpp | If member participates in update logic |
| 6 | Add to transfer data | `Transfer()` in .cpp | If collection supports cross-cell transfer |
| 7 | ClientInit | `ClientInit()` in .cpp | Client-only owned object handles |

## Important Notes

- **`Members()` drives automatic operations**: CRC, serialization, allocation, grow, destroy, and swap are all handled by template functions that iterate the `Members()` tuple. A member missing from the tuple will cause memory corruption.
- **`SharedMembers()` drives server CRC**: `SharedCollectionCrc()` uses `SharedMembers()` (when available) to exclude client-only fields. Players overrides this by calling `CollectionCrc()` directly with `SharedCrcMembers()` in Frame.cpp. `ServerCollectionRead()` reads only `SharedMembers()` from the server stream, then allocates full `Members()` zero-initialized so client-only pointers are valid but empty.
- **`AllocateAndCopyIds` helper**: For engine PostRender collections whose only persistent member is `puiIds`, `engine::AllocateAndCopyIds<T>()` handles the entire AllocateAndCopy in one call.
- **Destroy needs no changes**: `engine::DestroyElement()` and `engine::SwapElement()` operate on the `Members()` tuple automatically — swap-and-pop removal handles the new member as long as it's in the tuple (Step 1).
- **LogDifferences logs all shared fields**: This includes fields excluded from CRC via `SharedCrcMembers()` — LogDifferences has broader scope than CRC for desync diagnosis. Client-only fields are excluded.
- **`extern template`**: Collection headers declare `extern template struct Collection<T>` with explicit instantiation in the corresponding .cpp. No changes needed when adding members.

## See Also

- Engine collections: [/Engine/Source/Frame/Collections/CLAUDE.md](/Engine/Source/Frame/Collections/CLAUDE.md)
- Game collections: [/Projects/BrokenEngineSandbox/Source/Frame/Collections/CLAUDE.md](/Projects/BrokenEngineSandbox/Source/Frame/Collections/CLAUDE.md)
- CollectionMemory.h: [/Engine/Source/Frame/Collections/CollectionMemory.h](/Engine/Source/Frame/Collections/CollectionMemory.h) — SOA allocation/grow/swap/destroy templates
- Collection.h: [/Engine/Source/Frame/Collections/Collection.h](/Engine/Source/Frame/Collections/Collection.h) — CRC, serialization, base class
