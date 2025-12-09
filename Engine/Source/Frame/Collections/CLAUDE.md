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

**Initialization Phases**: All Interpolate structs have two static initialization methods called during engine startup:
- **`Register()`** - Called from `FrameInterpolateBase::Register()` for type registration and configuration. Each collection's Register() also calls `FrameInterpolateBase::RegisterGraphicsResources()` to self-register its GraphicsResources callback.
- **`GraphicsResources()`** - Called via callback registration. Collections register their GraphicsResources callback during Register() phase, then `FrameInterpolateBase::GraphicsResources()` iterates the callback vector to invoke them all.

PostRender structs do NOT have Register() or GraphicsResources().

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
- **`AddElement()`** - Increments counts for paired Interpolate/PostRender collections and returns spawn index. Used in Spawn() methods after capacity growth.

**When to use**:
- `ReallocateAndCopyMetadata()` - In AllocateAndCopy() static methods before Update() phase
- `GrowPairedCollections()` + `AddElement()` - In Spawn() for capacity management and index calculation

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
    int64_t iSpawnIndex = engine::AddElement(rCurrentInterpolate, rCurrentPostRender);

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
void Add(game::Frame& rFrame, id_t& rId)
{
    auto& rInterpolate = rFrame.interpolate.collection;
    auto& rPostRender = rFrame.postRender.collection;

    engine::GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());

    auto [uiSpawnIndex, newId] = engine::AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);

    // Collection-specific defaults
    rInterpolate.pMember[uiSpawnIndex] = defaultValue;
    rPostRender.puiIds[uiSpawnIndex] = newId;

    rId = newId;
}
```

**Usage Pattern - Remove() for Indexable Collections**:
```cpp
void Remove(game::Frame& rFrame, id_t& rId)
{
    auto& rInterpolate = rFrame.interpolate.collection;
    auto& rPostRender = rFrame.postRender.collection;

    engine::RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

    rId = {};
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
- **`CollectionFlags`** - Enum class defining compile-time configuration flags for collections. Currently supports `kIdToIndex` (enable ID-to-index mapping). Extensible for future collection features.
- **`RenderableFlags`** - Enum class defining compile-time configuration flags for renderable collections. Supports `kNone` (default), `kGltf` (glTF mode, implies GltfLayout), `kGltfShadow` (glTF mode with shadow pipeline, implies GltfLayout), `kLighting` (lighting mode, implies QuadLayout), `kAxisAlignedLighting` (axis-aligned lighting mode, implies AxisAlignedQuadLayout), `kBillboards` (billboard mode, implies BillboardLayout at 32 bytes), `kSmokeAxisAligned` (smoke emit pass with axis-aligned quads, implies AxisAlignedQuadLayout), `kSmoke` (smoke emit pass with generic quads, implies QuadLayout), and `kVisibleLights` (create visible lights pipeline, combinable with `kLighting` and `kAxisAlignedLighting`). Used by the Renderable mixin template.
- **`HasIdToIndex_v<T>`** - Type trait detecting if a collection type has idToIndexMap member. Used by template helpers to enable automatic indexable state copying.
- **`OptionaldToIndex<DerivedCollection, FLAGS>`** - Provides optional ID-to-index mapping support using CRTP pattern and C++20 requires clause. Template parameters: DerivedCollection (typename) for unique id_t typedef, FLAGS (`common::Flags<CollectionFlags>`) for feature selection. When `FLAGS & CollectionFlags::kIdToIndex`, automatically provides `using id_t = engine::id_t<DerivedCollection>` typedef and stores unordered_map<id_t, uint64_t> mapping IDs to array indices. Serializes only the map (size and key-value pairs), not the UUID counter which is stored in FramePostRenderBase. GetSortedKeys() ensures deterministic ordering during serialization. When kIdToIndex is not set, provides empty base (no overhead).
- **`TypeRegistry<TType>`** - Mixin providing static type registry for collections with type-based configuration sharing. Template parameter TType is the type definition struct (must be defined before the collection). Provides:
  - `using Type = TType` - Type alias for standardized access
  - `static inline std::vector<TType> sTypes` - Storage for registered types
  - `RegisterType(const TType& rType)` - Adds type to registry, returns uint8_t index (max 256 types)
  - `GetType(uint8_t uiIndex)` - Returns const reference to type at index
- **`Collection<DerivedCollection, FLAGS>`** - Base struct using CRTP pattern to provide common metadata (uiCount, uiCapacity, pData). Template parameters: DerivedCollection (typename) passed to OptionaldToIndex for unique id_t, FLAGS (`common::Flags<CollectionFlags>`, default `{}`) for feature selection. Inherits from OptionaldToIndex to gain optional ID mapping. Serialization order: uiCount → uiCapacity → idToIndexMap (if indexable), ensuring metadata is available before optional ID mapping restoration.

**When to use**: All game-specific collections inherit from `Collection<DerivedType>` or `Collection<DerivedType, CollectionFlags::kIdToIndex>` for indexable collections. Indexable collections automatically get `DerivedType::id_t` typedef. For type registry, also inherit from `TypeRegistry<MyType>`.

**Usage - Type registry**:
```cpp
struct MyType
{
    common::crc_t crc = 0;
    uint32_t uiColor = 0xFFFFFFFF;
};

struct MyInterpolate : public engine::Collection<MyInterpolate>,
                       public engine::TypeRegistry<MyType>
{
    // Inherits: sTypes, RegisterType(), GetType(), Type alias
};

// Registration:
uint8_t typeIndex = 0xFF;
MyInterpolate::RegisterType(typeIndex, {.crc = kTextureCrc, .uiColor = 0xFF0000FF});

// Lookup:
const MyType& type = MyInterpolate::GetType(typeIndex);
```

#### Renderable Mixin

Separate mixin template providing dynamic GPU buffer management for collections that render via glTF pipelines, lighting pipelines, axis-aligned lighting pipelines, or smoke emit passes:

- **`Renderable<T, NAME, FLAGS, GLTF_CRC, GLTF_MODEL_CRC, SMOKE_TEXTURE_CRC>`** - Template mixin providing GPU buffer and pipeline management. NAME is collection name passed as C++20 NTTP string. FLAGS is mandatory and controls both the rendering mode and layout size: glTF mode (`kGltf` or `kGltfShadow`, implies GltfLayout at 128 bytes), lighting mode (`kLighting`, implies QuadLayout at 160 bytes), axis-aligned lighting mode (`kAxisAlignedLighting`, implies AxisAlignedQuadLayout at 64 bytes), smoke emit mode (`kSmoke`, implies QuadLayout at 160 bytes), or smoke axis-aligned mode (`kSmokeAxisAligned`, implies AxisAlignedQuadLayout at 64 bytes). Template parameters GLTF_CRC, GLTF_MODEL_CRC, and SMOKE_TEXTURE_CRC default to 0, allowing mode-specific omission. Uses `if constexpr` for zero-overhead conditional branching between modes.
- **`AllocateDynamicBuffer()`** - Creates per-frame storage buffers via BufferManager
- **`AllocatePipelines()`** - Creates pipelines based on mode: glTF mode creates main + optional shadow pipeline, lighting mode creates lighting + optional visible lights pipeline, axis-aligned lighting mode creates axis-aligned lighting pipeline + optional visible lights pipeline, smoke modes create pipelines for the smoke emit render pass with texture CRC parameter. For visible lights, creates a separate buffer (176 bytes per VisibleLightQuadLayout) via CreateDynamicVisibleLightsBuffer().
- **`AllocateGltfPipelines()`** - Backward-compatible alias for glTF mode (static_assert prevents use with kLighting flag)
- **`ResizeBufferUpdateDescriptor()`** - Checks if buffer resize is needed and handles resize with descriptor set updates for all modes. For visible lights, also resizes the separate visible lights buffer. Uses VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT so no command buffer re-recording is needed.
- **`WritePipelineIndirectBuffers()`** - Writes indirect buffer counts to all pipelines for the current mode

**GraphicsResources() Pattern**: Collections inheriting from Renderable implement a static `GraphicsResources()` method in their Interpolate struct that calls `AllocatePipelines()` from the mixin. Collections without rendering have an empty `GraphicsResources()` implementation. This standardizes GPU resource initialization during engine startup via `FrameInterpolateBase::AllocateGraphicsResources()`.

**Usage - glTF mode**:
```cpp
struct MyCollection : public engine::Collection<MyCollection>,
                      public engine::Renderable<MyCollection, "MyCollection", RenderableFlags::kGltfShadow, kGltfCrc, kModelCrc>
```

**Usage - Lighting mode**:
```cpp
struct MyCollection : public engine::Collection<MyCollection>,
                      public engine::Renderable<MyCollection, "MyCollection", {RenderableFlags::kLighting, RenderableFlags::kVisibleLights}>
```

**Usage - Smoke axis-aligned mode**:
```cpp
struct MyCollection : public engine::Collection<MyCollection>,
                      public engine::Renderable<MyCollection, "MyCollection", {RenderableFlags::kSmokeAxisAligned}, 0, 0, kSmokeTextureCrc>
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
- Layer 2: `ReallocateAndCopyMetadata()`, `AddElement()` - AllocateAndCopy and Spawn patterns
- Indexable Helpers: `GrowPairedCollections()`, `AddIndexableElement()`, `RemoveIndexableElement()` - Add/Remove and Spawn for paired collections
- Layer 3: `SwapElement()` - Destroy pattern
- Layer 6: `CollectionCrc()`, `CollectionWrite()`, `CollectionRead()` - Serialization

**Internal Helpers** (only called by other template functions):
- Layer 1: `AssignAligned()`, `AssignAndCopyAligned()`
- Layer 2: `AllocateAndAssign()`, `ResetDataToNull()`, `GrowCapacityWithCopy()`
- Layer 4: `MultiCrc()`, `MultiWrite()`, `MultiRead()`, `AllocateAndRead()`
- Layer 5: OptionalIndexable and Collection member methods

### Colliders.h/cpp

Decoupled collision detection system enabling collision testing between game objects without direct coupling using category-based filtering.

**Purpose**: Provides centralized collision detection where game objects register colliders with categories and collision masks, then query collision results without knowing about each other's types. Eliminates cross-collection dependencies while maintaining deterministic replay.

**Architecture**: Two independent structures with ID-based registration and lookup:
- **CollidersInterpolate**: Position, radius, collision category, collision mask, flags, and damage data with ID-to-index mapping
- **CollidersPostRender**: ID storage for removal operations and static collision result storage

**Collision Categories and Masks**: Defined in game namespace (HealthDamage.h) using bit flags. Categories identify "what am I?" (kBlaster, kSpaceship, kPlayer). Masks identify "what can I collide with?" using bitwise combinations of categories. Early filtering during collision detection skips incompatible pairs before distance calculations.

**Collider Flags**: Defined in game namespace (HealthDamage.h) with behavior modifiers. kDestroyOnCollide marks objects that should only hit one target per frame. kAlreadyCollided prevents additional collisions in the same frame for destroy-on-collide objects.

**CollisionResult Structure**: Ephemeral collision data (not serialized) containing other collider ID, category, damage received, and contact point. Valid only during Collide phase.

**CollidersInterpolate Structure**:
- Inherits from `Collection<CollidersInterpolate, CollectionFlags::kIdToIndex>` for ID-based lookup
- Stores position (XMVECTOR), radius (float), category (uint16_t), collides-with mask (uint16_t), flags (uint8_t), and damage (float) arrays
- Static GraphicsResources() is empty (no rendering)
- Static Update() copies data from previous frame via ReallocateAndCopyMetadata
- Provides `collider_t` typedef via automatic ID generation from indexable collection
- Full serialization support via equality comparison and collection base methods

**CollidersPostRender Structure**:
- Inherits from `Collection<CollidersPostRender>` without indexing (uses Interpolate's idToIndexMap)
- Stores ID array for Remove() operations
- Static Add() creates new collider with category, mask, flags, and damage, returns strong-typed ID
- Static Remove() removes collider using ID lookup
- Static UpdatePosition() updates collider position during source collection's Update phase
- Static Collide() performs O(n²) pairwise collision detection with category-based early filtering before sphere-sphere distance tests
- Static collision result storage in `sCollisionResults` map cleared each frame
- Static HasCollision() and GetCollisions() query methods for accessing collision data during Collide phase

**Collision Detection Flow**:
1. Source collections (Blasters, Spaceships, Player) register colliders in their Spawn methods via Add() with category, mask, and flags
2. Source collections update collider positions in their Update methods via UpdatePosition()
3. Frame calls CollidersPostRender::Collide() during PostRenderCollide phase
4. Collide() clears kAlreadyCollided flags from previous frame, then performs pairwise checks with category-based early filtering
5. Compatible pairs undergo distance tests; on collision, both objects receive results if their masks allow
6. Destroy-on-collide objects are marked kAlreadyCollided to prevent multiple hits in same frame
7. Source collections query collision results via HasCollision()/GetCollisions() in their Collide methods
8. Source collections remove colliders in their Destroy methods via Remove()

**Decoupling Benefits**:
- Game objects don't know about each other's types or existence
- Adding new collision types requires only new category/mask definitions in game namespace
- Category-based filtering enables efficient collision layer management
- Collision algorithm can be optimized independently (spatial partitioning, broad phase, etc.)
- Clean separation between collision detection and collision response

**Usage Pattern**:
```cpp
// In game::HealthDamage.h - Define categories and masks
namespace CollisionCategory { inline constexpr uint16_t kBlaster = 0x0001; }
namespace CollisionMask { inline constexpr uint16_t kPlayerBlaster = CollisionCategory::kSpaceship; }
namespace ColliderFlags { inline constexpr uint8_t kDestroyOnCollide = 0x01; }

// In Blasters::Spawn() - Register collider with category, mask, and flags
collider_t colliderId = engine::CollidersPostRender::Add(rInterpolate, rPostRender, rFramePostRender,
    vecPosition, fRadius,
    game::CollisionCategory::kBlaster,
    game::CollisionMask::kPlayerBlaster,
    game::ColliderFlags::kDestroyOnCollide,
    fDamage);

// In Blasters::Update() - Sync position
engine::CollidersPostRender::UpdatePosition(rFrame.interpolate.colliders, colliderId, vecPosition);

// In Frame::PostRenderCollide() - Detect collisions with filtering
engine::CollidersPostRender::Collide(rFrame.interpolate.colliders, rFrame.postRender.colliders);

// In Blasters::Collide() - Query and respond
if (engine::CollidersPostRender::HasCollision(colliderId))
{
    auto* pCollisions = engine::CollidersPostRender::GetCollisions(colliderId);
    for (const auto& collision : *pCollisions)
    {
        // Handle collision based on collision.uiOtherCategory
    }
}

// In Blasters::Destroy() - Unregister collider
engine::CollidersPostRender::Remove(rFrame.interpolate.colliders, rFrame.postRender.colliders, colliderId);
```

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
- Static GraphicsResources() calls inherited AllocatePipelines() to create lighting and visible lights pipelines with dynamic storage buffers
- Static AllocateAndCopy() copies metadata and reallocates buffer using ReallocateAndCopyMetadata, automatically copying idToIndexMap via constexpr detection
- Static Update() copies type indices via memcpy with early-exit for null data - owner collections (Blasters, Player, etc.) write position and direction multiplier data every frame via idToIndexMap
- Instance Render() calls inherited ResizeAndUpdatePipelines() for dynamic buffer management, then submits dual rendering passes with AABB-based frustum culling. Uses inherited WritePipelineIndirectBuffers() for indirect draw buffer updates.
- Equality comparison and serialization via inherited Collection methods (includes type indices and direction multipliers)

**AreaLightsPostRender Structure**:
- Inherits from `Collection<AreaLightsPostRender>` without indexing
- ID array tracking area light identifiers using AreaLightsInterpolate::id_t
- Static RegisterType() and GetType() methods for type system management
- Static AllocateAndCopy() copies metadata and reallocates buffer using ReallocateAndCopyMetadata
- Static Update() processes updates with early-exit if pData is nullptr
- Static Add(rFrame, rId, uiTypeIndex) creates new area light with mandatory type index, generates ID via id_t::Generate(), stores type index, updates idToIndexMap in AreaLightsInterpolate, assigns ID to output parameter
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
    if (suiAreaLightTypeIndex == 0xFF)  // One-time registration
    {
        engine::AreaLightsPostRender::RegisterType(suiAreaLightTypeIndex,
        {
            .crc = data::kTexturesBlasterBC74pngCrc,
            .puiColors = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
            .pf2Texcoords = {{1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f}},
        });
    }
}

// In Spawn() - Create area light with registered type
rFrame.postRender.areaLights.Add(rFrame, rCurrentInterpolate.puiAreaLights[iSpawnIndex], suiAreaLightTypeIndex);

// In Update() - Sync position only (type data comes from registry)
uint64_t iAreaLightIndex = rAreaLights.IdToIndex(uiAreaLight);
rAreaLights.pVecPositions[iAreaLightIndex] = vecPosition;
```

### PointLights.h/cpp

Point light system with type-based configuration for circular lighting effects and visible light sprites.

**Purpose**: Manages dynamic point lights with position, rotation, and type-based configuration. Renders both ground-relative lighting effects and visible light sprites in world space.

**Architecture**: Two structures following the dual-phase Collection pattern:
- **PointLightsInterpolate**: Position, rotation, type index, and per-instance animatable properties (visible/lighting area and intensity) with ID-to-index mapping. Inherits from `Renderable<..., {kAxisAlignedLighting, kVisibleLights}>` for dual pipeline management and `TypeRegistry<PointLightsType>` for type registration.
- **PointLightsPostRender**: ID tracking for spawn/removal

**GPU Layouts**: Uses `AxisAlignedQuadLayout` (64 bytes) for lighting pass and `VisibleLightQuadLayout` (176 bytes) for visible light sprites. The Renderable mixin manages both dynamic buffers and pipelines.

**Type System**: Uses `TypeRegistry<PointLightsType>` mixin with `PointLightsType` defined at namespace level. Provides `RegisterType()`/`GetType()`/`sTypes` via inherited mixin. Stores color, visible/lighting area, and intensity configuration per type.

**Rendering**: Projects positions to base height for ground-relative lighting (AxisAlignedQuadLayout), uses original world positions for visible light sprites (VisibleLightQuadLayout). Performs visibility culling before populating both buffers.

### ControlledPointLights.h/cpp

Keyframe-animated point light system for time-based effects with automatic lifecycle management.

**Purpose**: Manages point lights that animate properties (area, intensity, rotation) over time using keyframe interpolation. Supports automatic destruction when animations complete, suitable for explosion flashes, projectile impacts, and other transient lighting effects.

**Architecture**: Two structures following the dual-phase Collection pattern:
- **ControlledPointLightsInterpolate**: Controller metadata and interpolated rendering state
- **ControlledPointLightsPostRender**: Controller type registration and auto-removal logic

**Controller Type System**: Static `sControllerTypes` vector with `RegisterControllerType()`/`GetControllerType()` pattern. Each ControllerType defines:
- Base point light type index (references PointLightsInterpolate::sTypes for color/texture)
- Keyframe count (2-4 keyframes) with time offsets
- Keyframe property values (visible/lighting area, intensity, rotation)
- Auto-destroy flag for automatic removal when animation completes

**Keyframe Interpolation**: Update() interpolates between keyframes based on elapsed time (fCurrentTime - fStartTime). Linear interpolation via ControllerKeyframe::Lerp() for smooth property transitions.

**Rendering Integration**: Shares the same GPU buffer as PointLights via coordinated Render() calls in FrameInterpolateBase::Render(). Both collections write to `mPointLightsStorageBuffers` with a shared count parameter, then indirect buffer is written once with combined count.

### Billboards.h/cpp

Screen-space billboard system for UI indicators with type-based configuration and offscreen handling.

**Purpose**: Manages billboards that render as screen-space quads with optional offscreen indicator behavior. Uses type system for texture and size configuration. Supports offscreen-only mode for indicators pointing to objects outside the viewport.

**Architecture**: Two structures following the dual-phase Collection pattern:
- **BillboardsInterpolate**: Position, type index, flags, rotation, and extra data for rendering with ID-to-index mapping. Inherits from `Renderable<..., {kBillboards}>` for billboard pipeline management.
- **BillboardsPostRender**: ID tracking and type registration for spawn/removal

**Billboard Flags**: `BillboardFlags` enum with `kOffscreenOnly` (only render when target is outside viewport), `kOffscreenRotate` (rotate billboard to point toward target direction), and game-specific type flags.

**Type System**: Static `sTypes` vector with `RegisterType()`/`GetType()` pattern. Stores texture CRC, size, and alpha configuration per type.

**Rendering**: Projects world positions to clip space. Handles offscreen-only billboards by clamping positions to screen edges. Calculates rotation for offscreen indicators to point toward target direction.

### Puffs.h/cpp

Controller-animated smoke puff system for fire-and-forget particle effects with automatic lifecycle management.

**Purpose**: Manages smoke puffs that animate properties (area, intensity, rotation) over time using keyframe interpolation. Supports automatic destruction when animations complete, suitable for explosions, impacts, and transient smoke effects.

**Architecture**: Two structures following the dual-phase Collection pattern:
- **PuffsInterpolate**: Position, type index, and per-instance animatable properties with controller state. Inherits from `Renderable<..., {kSmokeAxisAligned}>` for smoke emit pass rendering and `TypeRegistry<PuffsType>` for type registration.
- **PuffsPostRender**: AddControlled for spawning and Destroy for auto-removal

**Controller System**: Uses specialized PuffControllerType and PuffKeyframe structures for keyframe animation. PuffKeyframe stores semantically correct names (fArea, fIntensity, fRotation) instead of generic lighting properties. InterpolatePuffKeyframes() performs linear interpolation between keyframes.

**Type System**: Uses `TypeRegistry<PuffsType>` mixin with `PuffsType` defined at namespace level. Provides `RegisterType()`/`GetType()`/`sTypes` via inherited mixin. Stores texture CRC and color per type.

**Rendering**: Renders to smoke emit pass using axis-aligned quads. Uses AxisAlignedQuadLayout (64 bytes) for GPU buffer. Texture CRC specified via Renderable template parameter for pipeline creation.

### Trails.h/cpp

ID-indexed smoke trail system for externally-managed trail effects with smoothed rendering.

**Purpose**: Manages smoke trails attached to moving objects (projectiles, vehicles) with external ID tracking. Trails are created and removed by owning collections, not auto-destroyed.

**Architecture**: Two structures following the dual-phase Collection pattern:
- **TrailsInterpolate**: Position, type index, intensity, start time, and smoothing state with ID-to-index mapping via `CollectionFlags::kIdToIndex`. Inherits from `Renderable<..., {kSmoke}>` for smoke emit pass rendering and `TypeRegistry<TrailsType>` for type registration.
- **TrailsPostRender**: ID tracking, Add for spawning with output parameter, Remove for explicit destruction

**Type System**: Uses `TypeRegistry<TrailsType>` mixin with `TrailsType` defined at namespace level. Provides `RegisterType()`/`GetType()`/`sTypes` via inherited mixin. Type stores texture CRC, color, and width (shared across all trails of same type).

**ID Management**: Uses `trails_t` typedef (wraps TrailsInterpolate::id_t) for external tracking. Add() assigns ID to output parameter and initializes start time from frame time; Remove() accepts ID for lookup-based removal.

**Smoothing State**: Maintains previous and smoothed positions for calculating trail direction and preventing visual jitter. Sync() accepts `bFirstSync` parameter - when true (first call after Add), initializes previous and smoothed positions to the input position to prevent garbage values on first frame.

**Rendering**: Calculates quad vertices from current and previous positions with perpendicular width from type configuration. Uses QuadLayout (160 bytes) with 4 vertices per trail for proper orientation.

### Pushers.h/cpp

Physics-only force field system with zone-based spatial acceleration for efficient force queries.

**Purpose**: Manages repulsion force fields (explosion shockwaves, wind effects) with ID-based external tracking. No rendering - provides ApplyPush() for physics calculations. Uses spatial partitioning for O(1) force queries.

**Architecture**: Two structures following the dual-phase Collection pattern:
- **PushersInterpolate**: Position, radius, intensity, power, and flags with ID-to-index mapping via `CollectionFlags::kIdToIndex`. No Renderable mixin (physics-only). Provides SyncData struct and Sync() method for owner collections to update pusher state.
- **PushersPostRender**: ID tracking with Add/Remove for external management

**Zone System**: Global 50×50 grid covering 400×400 arena centered on player. SetupZones() builds spatial acceleration each frame; ApplyPush() queries single zone for efficient O(1) lookups instead of iterating all pushers.

**Pusher Flags**: `PusherFlags` enum with `kTypeDefault` and `kTypeMines` for filtering different pusher types during force queries. ApplyPush() accepts include/exclude flags.

**ID Management**: Uses `pusher_t` typedef (wraps PushersInterpolate::id_t). Add() assigns ID to output parameter; owner collections call Sync() to update pusher properties (position, radius, intensity, power, flags).

### Explosions.h/cpp

Composite explosion system managing multiple sub-effects (lights, puffs, trails, pushers, particles) with configurable explosion types.

**Purpose**: Manages complex explosions that spawn fire-and-forget effects (lights, puffs, particles) and maintain managed effects (trails with gravity, pushers with timed lifecycle). Uses type system for per-explosion-type configuration.

**Architecture**: Two structures following the dual-phase Collection pattern:
- **ExplosionsInterpolate**: Position, direction, timing, scaling percents, and managed trail/pusher state. Inherits from `TypeRegistry<ExplosionType>` for type registration. Trail arrays use SOA layout where `pTrails[j][i]` accesses explosion i's trail j.
- **ExplosionsPostRender**: Spawn() for creation and Destroy() for cleanup

**Registration System**: Static `Register()` method called from `FrameInterpolateBase::Register()` during engine initialization. Registers default explosion effect types:
- Point light type for explosion flash texture
- Primary/secondary light controller types with 3-keyframe animations (flash → bright → fade)
- Puff type for smoke texture
- Primary/secondary puff controller types with 2-keyframe animations (expand and fade)
- Trail type for smoke trails

**Getter Functions**: Static methods to retrieve registered controller indices for custom explosion configurations. Game code uses these in `*Interpolate::Register()` methods to construct explosion types:
- `GetPrimaryLightControllerTypeIndex()` / `GetSecondaryLightControllerTypeIndex()`
- `GetPrimaryPuffControllerTypeIndex()` / `GetSecondaryPuffControllerTypeIndex()`
- `GetTrailTypeIndex()`

**Usage Pattern - Registering Custom Explosion Types**:
```cpp
// In MyCollectionInterpolate::Register()
engine::ExplosionsInterpolate::RegisterType(suiMyExplosionTypeIndex,
{
    .uiPrimaryLightControllerTypeIndex = engine::ExplosionsInterpolate::GetPrimaryLightControllerTypeIndex(),
    .uiSecondaryLightControllerTypeIndex = engine::ExplosionsInterpolate::GetSecondaryLightControllerTypeIndex(),
    .uiPrimaryPuffControllerTypeIndex = engine::ExplosionsInterpolate::GetPrimaryPuffControllerTypeIndex(),
    .uiSecondaryPuffControllerTypeIndex = engine::ExplosionsInterpolate::GetSecondaryPuffControllerTypeIndex(),
    .uiTrailTypeIndex = engine::ExplosionsInterpolate::GetTrailTypeIndex(),
    .uiBaseParticleCount = 15,
    // ... other customizations
});
```

**ExplosionType System**: Static `sTypes` vector with `RegisterType()`/`GetType()` pattern. Configures controller indices for fire-and-forget effects, particle parameters, timing, pusher parameters, trail parameters, and secondary explosion offsets.

**Effect Categories**:
- **Fire-and-forget**: Point lights and puffs spawned via controller system (AddControlled), auto-destroyed by their own systems
- **Managed trails**: Up to 8 trails per explosion with gravity simulation updated in Sync(), removed when expired in Destroy()
- **Managed pushers**: Created/updated/removed during Update() based on timing parameters
- **GPU particles**: Spawned via ParticleManager with configurable velocity, color, and physics

**Phase Responsibilities**:
- **Interpolate::Update()**: Copies explosion state from previous frame
- **Interpolate::Sync()**: Updates trail positions with gravity simulation (visual interpolation only)
- **PostRender::Update()**: Manages pusher lifecycle (create/update/remove based on timing)
- **PostRender::Destroy()**: Removes expired trails and destroys explosions when all effects complete

**ExplosionFlags**: `kDestroysSelf` for auto-destruction, `kYellow`/`kRed` for particle color variation.

### Parent-Child Ownership: Sync Pattern

Collections that own other collections use the **Sync pattern** to encapsulate child field writes. This prevents grandparent collections from needing to know about grandchild collections.

**SyncData Struct**: Nested in child's Interpolate struct, contains only parent-provided values (not IDs):
```cpp
struct SyncData
{
    XMVECTOR vecPosition;
    uint8_t uiTypeIndex;
    // ... other parent-provided fields
};
```

**Sync Method**: Static method on child's Interpolate struct that writes own fields and propagates to owned children:
```cpp
static void Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
    auto& rSelf = rFrameInterpolate.collection;
    int64_t iIndex = rSelf.IdToIndex(id);

    // Write own fields
    rSelf.pVecPositions[iIndex] = rData.vecPosition;

    // Propagate to owned children (if any)
    ChildCollection::Sync(rFrameInterpolate, rSelf.puiChildIds[iIndex], {...});
}
```

**Usage in Parent Update()**:
```cpp
// Parent calls Sync() instead of writing child fields directly
ChildInterpolate::Sync(rCurrentFrameInterpolate, uiChildId, {
    .vecPosition = vecPosition,
    .uiTypeIndex = uiTypeIndex,
});
```

**Collections with SyncData/Sync**:
- **AreaLights**: uiTypeIndex, vecVisiblePositions[4]
- **Billboards**: vecPosition, uiTypeIndex, uiFlags, fRotation, fExtra
- **Pushers**: vecPosition, fRadius, fIntensity, fPower, flags
- **Sounds**: vecPosition, vecVelocity, uiCrc, fVolume, fPitch, fFadeOutTime
- **Trails**: vecPosition, fIntensity

**Benefit**: Grandparent collections (e.g., Spaceships) only call Sync() on their direct children (e.g., Targets). The child's Sync() automatically handles grandchildren (e.g., Billboards), maintaining proper encapsulation.

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
