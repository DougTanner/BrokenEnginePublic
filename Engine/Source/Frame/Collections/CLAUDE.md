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
- **`ResetDataToNull()`** - Releases buffer and zeros member pointers. Used by ReallocateAndCopyMetadata when previous frame has null data.
- **`ReallocateAndCopyMetadata()`** - Copies metadata (count, capacity, idToIndexMap) and reallocates buffer if capacity changed. Unlike ReallocateIfCapacityChanged, does not return early on null data. Used in AllocateAndCopy() static methods during the AllocateAndCopy phase to prepare collections before Update() runs.
- **`ReallocateIfCapacityChanged()`** - Synchronizes current frame storage with previous frame capacity. Automatically copies indexable state (idToIndexMap) for indexable collections. Returns false for null data (signals early return), true otherwise. Deprecated in favor of separate AllocateAndCopy phase.
- **`GrowCapacityWithCopy()`** - Internal helper that grows capacity while preserving existing data. Standard growth: 2 * capacity + 1. Used internally by GrowPairedCollections().
- **`IncrementCountsAndGetSpawnIndex()`** - Increments counts for paired Interpolate/PostRender collections and returns spawn index. Used in Spawn() methods after capacity growth.

**When to use**:
- `ReallocateAndCopyMetadata()` - In AllocateAndCopy() static methods before Update() phase
- `GrowPairedCollections()` + `IncrementCountsAndGetSpawnIndex()` - In Spawn() for capacity management and index calculation

**Usage Pattern - AllocateAndCopy()**:
```cpp
void AllocateAndCopy(CollectionType& rCurrent, const CollectionType& rPrevious)
{
    engine::ReallocateAndCopyMetadata(rCurrent, rPrevious, rCurrent.Members());
}
```

**Usage Pattern - Update()**:
```cpp
void Update(/* params */)
{
    CollectionType& rCurrent = /* ... */;

    // Early-exit if null (capacity already set in AllocateAndCopy phase)
    if (rCurrent.pData == nullptr)
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
    // Grow capacity if needed (uses GrowCapacityWithCopy internally)
    engine::GrowPairedCollections(rCurrentInterpolate, rCurrentPostRender, rCurrentInterpolate.Members(), rCurrentPostRender.Members());

    // Increment counts and get spawn index
    int64_t iSpawnIndex = engine::IncrementCountsAndGetSpawnIndex(rCurrentInterpolate, rCurrentPostRender);

    // Initialize new element at iSpawnIndex...
}
```

#### Indexable Collection Helpers

High-level helpers for Add() and Remove() operations on indexable collections. Template functions accept tuples directly (returned by `.Members()` method) and use `std::apply()` internally to unpack.

- **`GrowPairedCollections()`** - Grows paired Interpolate/PostRender collections if capacity is insufficient. Returns true if growth occurred.
- **`AddIndexableElement()`** - Increments counts, generates unique ID, updates idToIndexMap. Returns `{spawnIndex, newId}`.
- **`RemoveIndexableElement()`** - Complete remove operation: lookup, swap-and-pop, idToIndexMap update, count decrement. Requires PostRender to have `puiIds` member.

**Usage Pattern - Add() for Indexable Collections**:
```cpp
id_t Add(game::Frame& rFrame, uint8_t uiTypeIndex)
{
    auto& rInterpolate = rFrame.interpolate.collection;
    auto& rPostRender = rFrame.postRender.collection;

    engine::GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());

    auto [uiSpawnIndex, newId] = engine::AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);

    // Collection-specific defaults
    rInterpolate.pMember[uiSpawnIndex] = defaultValue;
    rPostRender.puiIds[uiSpawnIndex] = newId;

    return newId;
}
```

**Usage Pattern - Remove() for Indexable Collections**:
```cpp
void Remove(game::Frame& rFrame, id_t id)
{
    auto& rInterpolate = rFrame.interpolate.collection;
    auto& rPostRender = rFrame.postRender.collection;

    engine::RemoveIndexableElement(rInterpolate, rPostRender, id, rInterpolate.Members(), rPostRender.Members());
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
                engine::SwapElement(rCurrentInterpolate, i, rCurrentInterpolate.Members());
                engine::SwapElement(rCurrentPostRender, i, rCurrentPostRender.Members());
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
- **`AllocateAndRead()`** - Combines allocation with deserialization. Calls AllocateAndAssign if capacity > 0, otherwise calls ResetDataToNull. Then calls MultiRead. Used internally by CollectionRead().

**When to use**: Never called directly - these are internal helpers used by layer 6 collection-level functions.

#### Layer 5: Collection Base Class

Versioned metadata infrastructure with optional ID-to-index mapping and globally unique IDs for indexable collections:

- **`uuid_t`** - Global unique identifier with counter stored in FramePostRenderBase::uiNextUuid. Uses uint64_t internally with 0 representing invalid/uninitialized. Counter starts at 1 and uses simple increment for ID generation. Generate() accepts FramePostRenderBase& to access frame-local counter, ensuring deterministic replay. Provides IsValid(), Value(), comparison operators, and serialization support.
- **`id_t<Tag>`** - Strong-typed ID wrapper preventing implicit conversions between different collection types. Wraps uuid_t and uses Tag template parameter to ensure AreaLights::id_t cannot be mixed with other collection IDs. Generate() accepts FramePostRenderBase& to access frame-local counter. Provides IsValid(), ToUuid() for explicit conversion, comparison operators, and serialization support. Hash specialization enables use in unordered_map.
- **`CollectionFlags`** - Enum class defining compile-time configuration flags for collections. Currently supports `kNone` (default) and `kIdToIndex` (enable ID-to-index mapping). Extensible for future collection features.
- **`RenderableFlags`** - Enum class defining compile-time configuration flags for renderable collections. Supports `kNone` (default), `kGltf` (glTF mode, implies GltfLayout), `kGltfShadow` (glTF mode with shadow pipeline, implies GltfLayout), `kLighting` (lighting mode, implies QuadLayout), and `kVisibleLights` (create visible lights pipeline for lighting mode). Used by the Renderable mixin template.
- **`HasIdToIndex_v<T>`** - Type trait detecting if a collection type has idToIndexMap member. Used by template helpers to enable automatic indexable state copying.
- **`OptionaldToIndex<DerivedCollection, FLAGS>`** - Provides optional ID-to-index mapping support using CRTP pattern and C++20 requires clause. Template parameters: DerivedCollection (typename) for unique id_t typedef, FLAGS (`common::Flags<CollectionFlags>`) for feature selection. When `FLAGS & CollectionFlags::kIdToIndex`, automatically provides `using id_t = engine::id_t<DerivedCollection>` typedef and stores unordered_map<id_t, uint64_t> mapping IDs to array indices. Serializes only the map (size and key-value pairs), not the UUID counter which is stored in FramePostRenderBase. GetSortedKeys() ensures deterministic ordering during serialization. When kIdToIndex is not set, provides empty base (no overhead).
- **`Collection<DerivedCollection, FLAGS>`** - Base struct using CRTP pattern to provide common metadata (uiCount, uiCapacity, pData). Template parameters: DerivedCollection (typename) passed to OptionaldToIndex for unique id_t, FLAGS (`common::Flags<CollectionFlags>`, default `{}`) for feature selection. Inherits from OptionaldToIndex to gain optional ID mapping. Serialization order: uiCount → uiCapacity → idToIndexMap (if indexable), ensuring metadata is available before optional ID mapping restoration.

**When to use**: All game-specific collections inherit from `Collection<DerivedType>` or `Collection<DerivedType, CollectionFlags::kIdToIndex>` for indexable collections. Indexable collections automatically get `DerivedType::id_t` typedef.

#### Renderable Mixin

Separate mixin template providing dynamic GPU buffer management for collections that render via either glTF pipelines or lighting pipelines:

- **`Renderable<T, FLAGS, GLTF_CRC, GLTF_MODEL_CRC>`** - Template mixin providing GPU buffer and pipeline management. FLAGS is mandatory and controls both the rendering mode and layout size: glTF mode (`kGltf` or `kGltfShadow`, implies GltfLayout at 128 bytes) or lighting mode (`kLighting`, implies QuadLayout at 160 bytes). Template parameters GLTF_CRC and GLTF_MODEL_CRC default to 0, allowing lighting mode collections to omit them. Uses `if constexpr` for zero-overhead conditional branching between modes.
- **`AllocateDynamicBuffer()`** - Creates per-frame storage buffers via BufferManager
- **`AllocatePipelines()`** - Creates pipelines based on mode: glTF mode creates main + optional shadow pipeline, lighting mode creates lighting + optional visible lights pipeline. For visible lights, creates a separate buffer (176 bytes per VisibleLightQuadLayout) via CreateDynamicVisibleLightsBuffer().
- **`AllocateGltfPipelines()`** - Backward-compatible alias for glTF mode (static_assert prevents use with kLighting flag)
- **`ResizeBufferUpdateDescriptor()`** - Checks if buffer resize is needed and handles resize with descriptor set updates for both modes. For visible lights, also resizes the separate visible lights buffer. Uses VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT so no command buffer re-recording is needed.
- **`WritePipelineIndirectBuffers()`** - Writes indirect buffer counts to all pipelines for the current mode

**Usage - glTF mode**:
```cpp
struct MyCollection : public engine::Collection<MyCollection>,
                      public engine::Renderable<MyCollection, RenderableFlags::kGltfShadow, kGltfCrc, kModelCrc>
```

**Usage - Lighting mode**:
```cpp
struct MyCollection : public engine::Collection<MyCollection>,
                      public engine::Renderable<MyCollection, {RenderableFlags::kLighting, RenderableFlags::kVisibleLights}>
```

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
    checksum ^= engine::CollectionCrc(rCurrent.blasters, rCurrent.blasters.Members());
    // ... other collections ...
    return checksum;
}

// In Write() member function
void Write(std::ostream& rStream) const
{
    engine::CollectionWrite(rStream, blasters, blasters.Members());
    // ... other collections ...
}

// In Read() member function
void Read(std::istream& rStream)
{
    engine::CollectionRead(rStream, blasters, blasters.Members());
    // ... other collections ...
}
```

### External API vs Internal Helpers

**External API** (called by user code):
- Layer 2: `ReallocateAndCopyMetadata()`, `IncrementCountsAndGetSpawnIndex()` - AllocateAndCopy and Spawn patterns
- Indexable Helpers: `GrowPairedCollections()`, `AddIndexableElement()`, `RemoveIndexableElement()` - Add/Remove and Spawn for paired collections
- Layer 3: `SwapElement()` - Destroy pattern
- Layer 6: `CollectionCrc()`, `CollectionWrite()`, `CollectionRead()` - Serialization

**Internal Helpers** (only called by other template functions):
- Layer 1: `AssignAligned()`, `AssignAndCopyAligned()`
- Layer 2: `AllocateAndAssign()`, `ResetDataToNull()`, `GrowCapacityWithCopy()`
- Layer 4: `MultiCrc()`, `MultiWrite()`, `MultiRead()`, `AllocateAndRead()`
- Layer 5: OptionalIndexable and Collection member methods

### AreaLights.h/cpp

Area light system with phase-separated dynamic memory management and type-based configuration sharing for rendering and spawn/removal requests.

**Purpose**: Manages dynamic area lights created during gameplay (explosions, effects) with strict separation between rendering state and logic state. Uses type system to share configuration data (textures, colors, texcoords) across multiple area lights.

**Architecture**: Two independent structures inheriting from Collection with static type registry:
- **AreaLightsInterpolate**: Position and type index data for rendering with ID-to-index mapping
- **AreaLightsPostRender**: ID tracking for spawn/removal management and type registration

**AreaLightType System**:
- **AreaLightType struct**: Shared configuration data including texture CRC for mapping, vertex colors, and texture coordinates
- **Type Registration**: Static RegisterType() returns sequential uint8_t indices
- **Type Storage**: Static vector `sAreaLightTypes` holds registered types for program lifetime
- **Type Retrieval**: Static GetType(uint8_t) returns const reference to registered type data
- **Type Registration Pattern**: Game code registers types in collection constructors using Frame member initialization order
- **Type Index Size**: uint8_t allows up to 256 types (sufficient for game needs)
- **Registration Timing**: Types registered when Frame is constructed, leveraging C++ guaranteed member initialization order
- **Deterministic Ordering**: Registration order follows FramePostRender member declaration order in Frame.h (Player → Blasters → Spaceships)
- **Texture Mapping**: CRC field maps to texture indices via CrcToIndex() for both visible lights and area lights
- **Important**: Type indices are stable within program run but not serialized (reconstructed on each run via constructor calls). Changing member order in FramePostRender invalidates old replays/saves.

**AreaLightsInterpolate Structure**:
- Inherits from `Collection<AreaLightsInterpolate, CollectionFlags::kIdToIndex>` with indexable ID support using CRTP
- Inherits from `Renderable<AreaLightsInterpolate, {kLighting, kVisibleLights}>` for lighting pipeline management with dynamic buffer resizing (layout size inferred from flags)
- Automatically provides `AreaLightsInterpolate::id_t` typedef wrapping uuid_t with type safety
- Dynamically allocated position arrays (XMVECTOR), type index arrays (uint8_t), and direction multiplier arrays (XMVECTOR)
- Static AllocateGraphicsResources() calls inherited AllocatePipelines() to create lighting and visible lights pipelines with dynamic storage buffers
- Static AllocateAndCopy() copies metadata and reallocates buffer using ReallocateAndCopyMetadata, automatically copying idToIndexMap via constexpr detection
- Static Update() is minimal with early-exit for null data - owner collections (Blasters, Player, etc.) write position, type index, and direction multiplier data every frame via idToIndexMap
- Instance Render() calls inherited ResizeAndUpdatePipelines() for dynamic buffer management, then submits dual rendering passes with AABB-based frustum culling. Uses inherited WritePipelineIndirectBuffers() for indirect draw buffer updates.
- Equality comparison and serialization via inherited Collection methods (includes type indices and direction multipliers)

**AreaLightsPostRender Structure**:
- Inherits from `Collection<AreaLightsPostRender>` without indexing
- ID array tracking area light identifiers using AreaLightsInterpolate::id_t
- Static RegisterType() and GetType() methods for type system management
- Static AllocateAndCopy() copies metadata and reallocates buffer using ReallocateAndCopyMetadata
- Static Update() processes updates with early-exit if pData is nullptr
- Static Add(rFrame, uiTypeIndex) creates new area light with specified type, generates ID via id_t::Generate(), stores type index, updates idToIndexMap in AreaLightsInterpolate, returns new ID
- Static Remove() removes area light, uses swap-and-pop pattern with idToIndexMap update
- Equality comparison and serialization via inherited Collection methods

**ID Management**:
- Add() generates globally unique ID via AreaLightsInterpolate::id_t::Generate(rFrame.postRender) and inserts mapping in idToIndexMap
- Remove() uses idToIndexMap.at() for O(1) index lookup, updates map after swap
- idToIndexMap stores uint64_t indices mapped from strong-typed id_t keys
- Indexable state automatically preserved across frames via AllocateAndCopy phase before Update() runs
- UUID counter stored in FramePostRenderBase::uiNextUuid ensures deterministic replay

**Type System Usage Example**:
```cpp
// In Blasters.h - Declare constructor and static member
struct BlastersPostRender : public Collection<BlastersPostRender>
{
    BlastersPostRender();
    static uint8_t suiAreaLightTypeIndex;
};

// In Blasters.cpp - Register type in constructor (called during Frame construction)
uint8_t BlastersPostRender::suiAreaLightTypeIndex = 255;

BlastersPostRender::BlastersPostRender()
{
    if (suiAreaLightTypeIndex == 255)  // One-time registration
    {
        static const engine::AreaLightType kType =
        {
            .crc = data::kTexturesBlasterBC74pngCrc,
            .puiColors = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
            .pf2Texcoords = {{1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f}},
        };
        suiAreaLightTypeIndex = engine::AreaLightsPostRender::RegisterType(kType);
    }
}

// In Spawn() - Create area light with registered type
rCurrentInterpolate.puiAreaLights[iSpawnIndex] = rFrame.postRender.areaLights.Add(rFrame, suiAreaLightTypeIndex);

// In Update() - Sync position only (type data comes from registry)
uint64_t iAreaLightIndex = rAreaLights.IdToIndex(uiAreaLight);
rAreaLights.pVecPositions[iAreaLightIndex] = vecPosition;
```

### Adding New Members to Collections

When adding new members to collection structures, follow the 5-step pattern documented in the **add-collection-member** skill. Use `/add-collection-member` or invoke the skill to see the complete checklist with examples.

**Quick Summary**:
1. Add member pointer to struct and update `Members()` method to include it in `std::tie()`
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
- Tuple-based API with `Members()` methods returning `std::tie()` provides compile-time member list verification
- Internal `std::apply()` unpacking enables arbitrary member counts without code duplication
- Fold expressions guarantee synchronized operations across parallel arrays
- CRTP-based version tracking ensures schema compatibility
- Optional indexing is controlled by template parameter with zero-cost abstraction when disabled

## See Also
- `/Engine/Source/Frame/Pools/` - Object pool management
- `/Projects/*/Source/Frame/Collections/` - Game-specific implementations
- `/Engine/Source/Frame/UpdateList.h` - Update phase definitions
- `/Engine/Source/Frame/FrameBase.h` - Frame versioning system
