---
name: add-collection-member
description: Reference guide for adding new members to dynamically-allocated Structure-of-Arrays collection structures (both engine and game collections).
allowed-tools: [Read, Edit]
---

# Adding New Members to Collections

Reference guide for properly adding new members to collection structures. Collections use dynamically-allocated Structure-of-Arrays layout with macro-based serialization.

## When to Use

Use this guide when adding a new member (array) to any collection structure:
- **Engine collections**: AreaLightsInterpolate (in `/Engine/Source/Frame/Collections/`)
- **Game collections**: BlastersInterpolate, BlastersPostRender, SpaceshipsInterpolate, SpaceshipsPostRender (in `/Projects/*/Source/Frame/Collections/`)

## Engine Collections (2-Step Pattern)

Engine collections use stream operators for serialization and don't require explicit load/save or spawn initialization.

### Step 1: Update Macro List (Header File)

Add the new member to the macro and declare the pointer:

```cpp
#define AREA_LIGHTS_INTERPOLATE_LIST(a) a.pVecPositions, a.pfIntensities
XMVECTOR* __restrict pVecPositions = nullptr;
float* __restrict pfIntensities = nullptr; // New member
```

The macro is used by:
- `CollectionCrc()` for CRC calculation
- `CollectionWrite()` for serialization
- `CollectionRead()` for deserialization
- `AllocateAndRead()` for memory allocation

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

### Step 1: Update Macro List (Header File)

Add the new member to the macro and declare the pointer:

```cpp
#define BLASTERS_POST_RENDER_LIST(a) a.pFlags, a.pVecVelocities, a.pfSpeed
BlasterFlags_t* __restrict pFlags = nullptr;
XMVECTOR* __restrict pVecVelocities = nullptr;
float* __restrict pfSpeed = nullptr; // New member
```

The macro is used by:
- Parent frame's `CollectionCrc()` calls for CRC calculation
- Parent frame's `CollectionWrite()` calls for serialization
- Parent frame's `CollectionRead()` calls for deserialization
- `ReallocateIfCapacityChanged()` for Update() reallocation
- `GrowCapacityWithCopy()` for Spawn() capacity growth
- `AllocateAndRead()` for deserialization memory allocation

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

- **Version numbers**: Increment collection version constants when adding/removing members for save file compatibility
- **Macro drives everything**: The macro list automatically handles memory allocation, serialization, and CRC calculation via template functions
- **Missing steps cause failures**:
  - Missing macro update → Compilation errors, memory corruption
  - Missing equality → Deterministic replay validation breaks
  - Missing load/save → State not propagated between frames
  - Missing spawn init → Undefined behavior for new objects
- **Consistency is critical**: All members must be in the macro, equality operator, load/save, and spawn initialization

## See Also

- Engine collections: [/Engine/Source/Frame/Collections/CLAUDE.md](/Engine/Source/Frame/Collections/CLAUDE.md)
- Game collections: [/Projects/BrokenEngineSandbox/Source/Frame/Collections/CLAUDE.md](/Projects/BrokenEngineSandbox/Source/Frame/Collections/CLAUDE.md)
