# `/Engine/Source/Frame/Collections/`

Template utilities for managing dynamically-allocated Structure-of-Arrays collections with efficient memory management and serialization support.

## Architecture Overview

Collections.h provides a layered template library for Structure-of-Arrays (SOA) memory management. The design follows a clear hierarchy from low-level pointer arithmetic to high-level collection operations, with each layer building on the previous one.

### Layered Architecture

The template functions are organized into six behavioral categories:

1. **Low-Level Memory Alignment** - 64-byte pointer alignment and positioning
2. **High-Level Allocation & Reallocation** - Orchestrated memory management
3. **Element Manipulation** - Individual element operations
4. **Multi-Array Serialization** - Synchronized array I/O
5. **Collection Base Class** - Versioned metadata infrastructure with optional indexing
6. **Collection-Level Patterns** - Complete collection operations (external API)

**Design Philosophy**: Internal helpers (layers 1-4) compose into external API functions (layer 6) that user code calls. The base class (layer 5) provides common metadata and infrastructure.

## Core Files

### Collections.h

Template utilities for memory management supporting Structure-of-Arrays layout with dynamic allocation and optional ID-to-index mapping.

#### Layer 1: Low-Level Memory Alignment Helpers

Internal building blocks for pointer arithmetic and SIMD-optimized alignment:

- **`AssignAligned<T>()`** - Aligns pointer to 64-byte boundary, assigns to aligned position, advances current position. Used during initial allocation.
- **`AssignAndCopyAligned<T>()`** - Aligns pointer, copies existing data, advances position. Used during capacity growth to preserve elements.

**When to use**: Never called directly - these are internal building blocks used by higher-level helpers.

#### Layer 2: High-Level Allocation & Reallocation Helpers

Orchestrated memory management for Structure-of-Arrays collections:

- **`AllocateAndAssign()`** - Allocates contiguous buffer and positions multiple member array pointers within it. Used during initial allocation and deserialization. Automatically calculates buffer size using fold expressions.
- **`ResetDataToNull()`** - Releases buffer and zeros member pointers. Used by ReallocateIfCapacityChanged when previous frame has null data.
- **`ReallocateIfCapacityChanged()`** - Synchronizes current frame storage with previous frame capacity. Returns false for null data (signals early return), true otherwise. Used in Update() methods.
- **`GrowCapacityWithCopy()`** - Grows capacity while preserving existing data. Standard growth: 2 * capacity + 1. Used in Spawn() methods when adding elements would exceed capacity.
- **`CalculateGrowthCapacity()`** - Checks if capacity growth is needed for spawning. Returns new capacity (2 * capacity + 1) if growth needed, 0 otherwise. Used in Spawn() methods.
- **`IncrementCountsAndGetSpawnIndex()`** - Increments counts for paired Interpolate/PostRender collections and returns spawn index. Used in Spawn() methods after capacity growth.

**When to use**:
- `ReallocateIfCapacityChanged()` - First line of every Update() method
- `CalculateGrowthCapacity()` + `IncrementCountsAndGetSpawnIndex()` - In Spawn() for capacity management and index calculation
- `GrowCapacityWithCopy()` - Called explicitly when growth is needed

**Usage Pattern - Update()**:
```cpp
void Update(/* params */)
{
    CollectionType& rCurrent = /* ... */;
    const CollectionType& rPrevious = /* ... */;

    // Reallocate if needed, early-exit if null
    if (!engine::ReallocateIfCapacityChanged(rCurrent, rPrevious, COLLECTION_LIST(rCurrent)))
    {
        return;
    }

    // Process elements...
}
```

**Usage Pattern - Spawn()**:
```cpp
void Spawn(/* params */)
{
    // Check if growth needed
    int64_t iNewCapacity = engine::CalculateGrowthCapacity(rCurrentInterpolate);
    if (iNewCapacity > 0)
    {
        ASSERT(rCurrentInterpolate.iCount == rCurrentPostRender.iCount);
        engine::GrowCapacityWithCopy(rCurrentInterpolate, iNewCapacity, rCurrentInterpolate.iCount, INTERPOLATE_LIST(rCurrentInterpolate));
        engine::GrowCapacityWithCopy(rCurrentPostRender, iNewCapacity, rCurrentPostRender.iCount, POST_RENDER_LIST(rCurrentPostRender));
    }

    // Increment counts and get spawn index
    int64_t iSpawnIndex = engine::IncrementCountsAndGetSpawnIndex(rCurrentInterpolate, rCurrentPostRender);

    // Initialize new element at iSpawnIndex...
}
```

#### Layer 3: Element Manipulation

Functions for modifying individual elements:

- **`SwapElement()`** - Swaps element at index i with last element. Used in Destroy() for O(1) unordered removal. Does NOT decrement count or bounds-check - caller handles count decrement and index re-checking.

**Usage Pattern - Destroy()**:
```cpp
void Destroy(/* params */)
{
    for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
    {
        if (shouldDestroy)
        {
            // Only swap if not already the last element
            if (rCurrentInterpolate.iCount - 1 > i)
            {
                engine::SwapElement(rCurrentInterpolate, i, INTERPOLATE_LIST(rCurrent));
                engine::SwapElement(rCurrentPostRender, i, POST_RENDER_LIST(rCurrent));
                --i;  // Re-check this index (new element swapped in)
            }
            --rCurrentInterpolate.iCount;
            --rCurrentPostRender.iCount;
        }
    }
}
```

#### Layer 4: Multi-Array Serialization Helpers

Synchronized operations on parallel arrays using fold expressions:

- **`MultiCrc()`** - Computes XOR'd CRC of multiple member arrays. Returns 0 if count == 0. Used internally by CollectionCrc().
- **`MultiWrite()`** - Serializes multiple member arrays to stream in order. Used internally by CollectionWrite().
- **`MultiRead()`** - Deserializes multiple member arrays from stream (must match write order). Assumes arrays already allocated. Used internally by CollectionRead().
- **`AllocateAndRead()`** - Combines allocation with deserialization. Calls AllocateAndAssign if capacity > 0, otherwise sets pData to nullptr. Then calls MultiRead. Used internally by CollectionRead().

**When to use**: Never called directly - these are internal helpers used by layer 6 collection-level functions.

#### Layer 5: Collection Base Class

Versioned metadata infrastructure with optional ID-to-index mapping for indexable collections:

- **`VersionIncrementor<VERSION>`** - CRTP helper that increments FrameBase::smiVersion during static initialization. Ensures global frame version reflects all collection schema changes.
- **`OptionalIndexable<INDEXABLE, T>`** - Provides optional ID-to-index mapping support. If INDEXABLE is true, stores unordered_map of ID to index and next ID counter. Implements Write(), Read(), Crc(), and operator==() for serialization and validation. When INDEXABLE is false, provides empty base (no overhead).
- **`Collection<VERSION, INDEXABLE, T>`** - Base struct providing common metadata (iCount, iCapacity, pData) and combining VersionIncrementor with OptionalIndexable for complete infrastructure. Inherits serialization methods from both parents.

**When to use**: All game-specific collections inherit from `Collection<VERSION>` or `Collection<VERSION, true, id_type>` for indexable collections.

#### Layer 6: Collection-Level Pattern Helpers (External API)

High-level API functions that user code calls directly:

- **`CollectionCrc()`** - Computes complete CRC (metadata + all member arrays). Used in static Crc() methods for deterministic replay validation. Combines base class Crc() + MultiCrc().
- **`CollectionWrite()`** - Writes complete collection to stream (metadata + all member arrays). Used in operator<< overloads for save file serialization. Combines base class Write() + MultiWrite().
- **`CollectionRead()`** - Reads complete collection from stream (metadata + all member arrays). Used in operator>> overloads to restore from save files. Combines base class Read() + AllocateAndRead().

**Usage Pattern - Serialization**:
```cpp
// In static Crc() method
common::crc_t Crc(const Frame& rCurrent)
{
    common::crc_t checksum = 0;
    checksum ^= engine::CollectionCrc(rCurrent.blasters, BLASTERS_LIST(rCurrent.blasters));
    // ... other collections ...
    return checksum;
}

// In Write() member function
void Write(std::ostream& rStream) const
{
    engine::CollectionWrite(rStream, blasters, BLASTERS_LIST(blasters));
    // ... other collections ...
}

// In Read() member function
void Read(std::istream& rStream)
{
    engine::CollectionRead(rStream, blasters, BLASTERS_LIST(blasters));
    // ... other collections ...
}
```

### External API vs Internal Helpers

**External API** (called by user code):
- Layer 2: `ReallocateIfCapacityChanged()`, `CalculateGrowthCapacity()`, `IncrementCountsAndGetSpawnIndex()`, `GrowCapacityWithCopy()` - Update and Spawn patterns
- Layer 3: `SwapElement()` - Destroy pattern
- Layer 6: `CollectionCrc()`, `CollectionWrite()`, `CollectionRead()` - Serialization

**Internal Helpers** (only called by other template functions):
- Layer 1: `AssignAligned()`, `AssignAndCopyAligned()`
- Layer 2: `AllocateAndAssign()`, `ResetDataToNull()`
- Layer 4: `MultiCrc()`, `MultiWrite()`, `MultiRead()`, `AllocateAndRead()`
- Layer 5: OptionalIndexable and Collection member methods

### AreaLights.h

Area light system with phase-separated dynamic memory management for rendering and spawn/removal requests.

**Purpose**: Manages dynamic area lights created during gameplay (explosions, effects) with strict separation between rendering state and logic state.

**Architecture**: Two independent structures inheriting from Collection:
- **AreaLightsInterpolate**: Position data for rendering
- **AreaLightsPostRender**: ID tracking for spawn/removal management

**AreaLightsInterpolate Structure**:
- Inherits from `Collection<kiAreaLightsInterpolateVersion, true>` with indexable ID support
- Dynamically allocated position arrays (XMVECTOR)
- Static Render() method handles area light rendering
- Equality comparison and serialization via inherited Collection methods

**AreaLightsPostRender Structure**:
- Inherits from `Collection<kiAreaLightsPostRenderVersion>`
- ID array tracking area light identifiers
- Static Update() method manages frame-to-frame state transitions
- Static Add() method creates new area light with spawn request
- Static Remove() method destroys area light with removal request
- Equality comparison and serialization via inherited Collection methods

### Adding New Members to Collections

When adding new members to collection structures, follow the 5-step pattern documented in the **add-collection-member** skill. Use `/add-collection-member` or invoke the skill to see the complete checklist with examples.

**Quick Summary**:
1. Update macro list in header file
2. Add equality comparison in operator==()
3. Load member in Update() method
4. Save member in Update() method
5. Initialize member in Spawn() method

Missing any step will cause compilation errors, runtime crashes, or determinism failures. See the skill for detailed instructions and code examples.

## Design Principles

### Layered Composition

The library uses a clear layered design where each level builds on the previous:
- **Low-level helpers** (layer 1) handle pointer arithmetic and alignment
- **Mid-level helpers** (layers 2-4) compose low-level operations into patterns
- **Base class** (layer 5) provides common infrastructure
- **High-level API** (layer 6) exposes complete operations to user code

This architecture ensures:
- **Single Responsibility**: Each function has one clear purpose
- **Composability**: Higher-level operations build on lower-level primitives
- **Testability**: Each layer can be validated independently
- **Maintainability**: Changes propagate cleanly through the hierarchy

### Memory Efficiency

- Single contiguous allocation per structure with 64-byte alignment for cache efficiency
- RAII cleanup via `common::AlignedUniquePtr<std::byte>`
- Automatic buffer size calculation using fold expressions over member pointer types
- Structure-of-Arrays layout enables efficient vectorized operations
- Growth strategy (2 * capacity + 1) balances allocation overhead with memory waste
- Optional ID-to-index mapping with minimal overhead for non-indexed collections

### Determinism

- Helper functions operate deterministically on input data
- Serialization helpers preserve exact state for replay validation
- CRC computation combines all member arrays via XOR for fast validation
- Consistent patterns ensure identical behavior across all collections
- Optional indexing preserves deterministic key ordering via sorted serialization

### Type Safety

- Template parameters ensure compile-time type checking
- Variadic templates enable arbitrary member counts without code duplication
- Fold expressions guarantee synchronized operations across parallel arrays
- CRTP-based version tracking ensures schema compatibility
- Optional indexing is controlled by template parameter with zero-cost abstraction when disabled

## See Also
- `/Engine/Source/Frame/Pools/` - Object pool management
- `/Projects/*/Source/Frame/Collections/` - Game-specific implementations
- `/Engine/Source/Frame/UpdateList.h` - Update phase definitions
- `/Engine/Source/Frame/FrameBase.h` - Frame versioning system
