---
name: add-collection-member
description: Reference guide for adding new members to dynamically-allocated Structure-of-Arrays collection structures (both engine and game collections).
allowed-tools: [Read, Edit]
---

# Adding New Members to Collections

Reference guide for properly adding new members to collection structures. Collections use dynamically-allocated Structure-of-Arrays layout with.

## When to Use

Use this guide when adding a new member (array) to any collection structure:
- **Engine collections**: AreaLightsInterpolate (in `/Engine/Source/Frame/Collections/`)
- **Game collections**: BlastersInterpolate, BlastersPostRender, SpaceshipsInterpolate, SpaceshipsPostRender (in `/Projects/*/Source/Frame/Collections/`)

## Engine Collections (2-Step Pattern)

Engine collections use stream operators for serialization and don't require explicit load/save or spawn initialization.

### Step 1: Add to Members() Method (Header File)

Declare the pointer and add it to the `Members()` method:

```cpp
XMVECTOR* __restrict pVecPositions = nullptr;
float* __restrict pfIntensities = nullptr; // New member
auto Members(this auto&& rSelf) { return std::tie(rSelf.pVecPositions, rSelf.pfIntensities); }
```

The `Members()` method returns a `std::tie()` of all SOA member pointers and is used by:
- `CollectionCrc()` for CRC calculation
- `CollectionWrite()` for serialization
- `CollectionRead()` for deserialization
- `Allocate()` for memory allocation

### Step 2: Add Equality Comparison (CPP File)

Add `common::BreakOnNotEqual()` in `operator==()`:

```cpp
bool AreaLightsInterpolate::operator==(const AreaLightsInterpolate& rOther) const
{
    bool bEqual = true;
    bEqual &= CompareCountAndCapacity(rOther);

    for (int64_t i = 0; i < iCount; ++i)
    {
        bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
        bEqual &= common::BreakOnNotEqual(pfIntensities[i], rOther.pfIntensities[i]); // New member
    }

    return bEqual;
}
```

This enables deterministic replay validation.

## Game Collections (5-Step Pattern)

Game collections have explicit Update() load/save and Spawn() initialization.

### Step 1: Add to Members() Method (Header File)

Declare the pointer and add it to the `Members()` method:

```cpp
BlasterFlags_t* __restrict pFlags = nullptr;
XMVECTOR* __restrict pVecVelocities = nullptr;
float* __restrict pfSpeed = nullptr; // New member
auto Members(this auto&& rSelf) { return std::tie(rSelf.pFlags, rSelf.pVecVelocities, rSelf.pfSpeed); }
```

The `Members()` method returns a `std::tie()` of all SOA member pointers and is used by:
- `CollectionCrc()` for CRC calculation
- `CollectionWrite()` for serialization
- `CollectionRead()` for deserialization
- `Allocate()` for AllocateAndCopy() phase
- `GrowPairedCollections()` for Spawn() capacity growth
- `DestroyElement()` for Destroy() removal

### Step 2: Add Equality Comparison (CPP File)

Add `common::BreakOnNotEqual()` in `operator==()`:

```cpp
bool BlastersPostRender::operator==(const BlastersPostRender& rOther) const
{
    bool bEqual = true;
    bEqual &= CompareCountAndCapacity(rOther);

    for (int64_t i = 0; i < iCount; ++i)
    {
        bEqual &= common::BreakOnNotEqual(pFlags[i], rOther.pFlags[i]);
        bEqual &= common::BreakOnNotEqual(pVecVelocities[i], rOther.pVecVelocities[i]);
        bEqual &= common::BreakOnNotEqual(pfSpeed[i], rOther.pfSpeed[i]); // New member
    }

    return bEqual;
}
```

### Step 3: Load Member in Update() (CPP File)

Load from previous frame at the beginning of the update loop:

```cpp
void BlastersPostRender::Update(...)
{
    // ... ReallocateIfCapacityChanged code ...

    for (int64_t i = 0; i < rCurrent.iCount; ++i)
    {
        BlasterFlags_t flag = rPrevious.pFlags[i];
        XMVECTOR vecVelocity = rPrevious.pVecVelocities[i];
        float fSpeed = rPrevious.pfSpeed[i]; // Step 3

        // ... processing logic ...
    }
}
```

### Step 4: Save Member in Update() (CPP File)

Save to current frame at the end of the update loop:

```cpp
void BlastersPostRender::Update(...)
{
    for (int64_t i = 0; i < rCurrent.iCount; ++i)
    {
        // ... load and processing ...

        rCurrent.pFlags[i] = flag;
        rCurrent.pVecVelocities[i] = vecVelocity;
        rCurrent.pfSpeed[i] = fSpeed; // Step 4
    }
}
```

### Step 5: Initialize in Spawn() (CPP File)

Set a good default value when spawning:

```cpp
void XM_CALLCONV BlastersPostRender::Spawn(...)
{
    // ... capacity growth and index calculation ...

    rCurrentInterpolate.pVecPositions[iSpawnIndex] = vecPosition;

    rCurrentPostRender.pFlags[iSpawnIndex] = {};
    rCurrentPostRender.pVecVelocities[iSpawnIndex] = vecVelocity;
    rCurrentPostRender.pfSpeed[iSpawnIndex] = 0.0f; // Step 5
}
```

## Important Notes

- **Complete all steps before moving on**: For game collections, complete all 5 steps for each new member before proceeding to other work. Partial completion causes compilation errors, memory corruption, or determinism failures.
- **Version numbers**: Increment collection version constants when adding/removing members for save file compatibility
- **Members() drives everything**: The `Members()` method automatically handles memory allocation, serialization, and CRC calculation via template functions
- **Missing steps cause failures**:
  - Missing Members() update → Compilation errors, memory corruption
  - Missing equality → Deterministic replay validation breaks
  - Missing load/save → State not propagated between frames
  - Missing spawn init → Undefined behavior for new objects
- **Consistency is critical**: All members must be in the Members() method, equality operator, load/save, and spawn initialization

## See Also

- Engine collections: [/Engine/Source/Frame/Collections/CLAUDE.md](/Engine/Source/Frame/Collections/CLAUDE.md)
- Game collections: [/Projects/BrokenEngineSandbox/Source/Frame/Collections/CLAUDE.md](/Projects/BrokenEngineSandbox/Source/Frame/Collections/CLAUDE.md)
