# /Engine/Source/Frame/Collections/

Template utilities and concrete collections for managing dynamically-allocated Structure-of-Arrays (SOA) data with efficient memory management, serialization, and GPU rendering support.

## Architecture Overview

The collection system provides a layered template library for SOA memory management:

1. **Collection.h** - Core template infrastructure with memory alignment, allocation, element manipulation, serialization, and base classes
2. **Renderable.h** - GPU buffer and pipeline management mixin for renderable collections
3. **Concrete Collections** - Engine-level collections for lights, effects, audio, and physics

## Core Templates (Collection.h)

**Collection<T, FLAGS>** - Base class using CRTP providing count, capacity, and contiguous data buffer. Optional `CollectionFlags::kIdToIndex` enables ID-to-index mapping for stable external references.

**TypeRegistry<TType>** - Mixin for static type configuration sharing (textures, colors, etc.) across collection instances.

**ControllerTypeRegistry<T, TControllerType>** - Mixin for keyframe animation support with time-based property interpolation.

**Template Helpers** - Functions for allocation (`Allocate`, `AllocateAndAssign`), element operations (`SwapElement`, `DestroyElement`, `AddElement`), indexable collections (`GrowPairedCollections`, `AddIndexableElement`, `RemoveIndexableElement`), and serialization (`CollectionCrc`, `CollectionWrite`, `CollectionRead`). UUID generation via `uuid_t::Generate(FramePostRenderBase&)` uses the per-Frame counter in FramePostRenderBase, accessed through `rFrame.postRender`.

## Renderable Mixin (Renderable.h)

**Renderable<T, NAME, FLAGS, GLTF_CRC>** - Provides dynamic GPU buffer management for collections that render via pipelines. FLAGS controls rendering mode: glTF (`kGltf`, `kGltfShadow`), lighting (`kLighting`, `kAxisAlignedLighting`), visible lights (`kVisibleLights`), smoke (`kSmoke`, `kSmokeAxisAligned`), billboards (`kBillboards`), or hex shields (`kHexShields`). For glTF mode, the CRC can be specified either as the GLTF_CRC template parameter or passed at runtime via `AllocatePipelines(gltfCrc)`. The runtime CRC overload avoids header dependencies on Data/Gltf.h in collection headers.

**Buffer Bounds Validation**: Collections retrieve GPU buffers via `GetDynamicStorageBuffer<T>()` which returns both the mapped pointer and buffer capacity. Render methods assert that the write count does not exceed the buffer capacity before writing, catching overflow bugs early.

## Engine Collections

| Collection | Rendering | ID-Indexed | Purpose |
|------------|-----------|------------|---------|
| **AreaLights** | Lighting + VisibleLights | Yes | Quad-based area lights for projectiles/effects |
| **Billboards** | Billboards | Yes | Screen-space UI indicators with offscreen handling |
| **PointLights** | AxisAlignedLighting + VisibleLights | Yes | Circular point lights with keyframe animation |
| **Puffs** | SmokeAxisAligned | No | Fire-and-forget smoke puffs with controller animation |
| **Trails** | Smoke | Yes | Externally-managed smoke trails with position smoothing |
| **HexShields** | HexShields + HexShieldsLighting | Yes | Geodesic shield meshes with directional damage |
| **Explosions** | None | No | Composite effects spawning lights, puffs, and trails |
| **Pushers** | None | Yes | Physics force fields with zone-based spatial queries |
| **Sounds** | None | Yes | 3D spatial audio sources |

## Dual-Phase Pattern

All collections follow the Interpolate/PostRender dual-phase pattern:
- **Interpolate struct**: Rendering state, position/transform data, optional controller animation
- **PostRender struct**: Logic operations (Add, Remove, Spawn, Destroy), ID tracking

Static methods: `Register()`, `GraphicsResources()`, `AllocateAndCopy()`, `Update()`, phase callbacks.

## Sync Pattern

Collections with external ownership use `SyncData` structs and `Sync()` methods to encapsulate writes, enabling parent collections to update child state without exposing internal details. Used by AreaLights, Billboards, PointLights, Pushers, Sounds, Trails, and HexShields.

**Critical:** Owners MUST call `Sync()` every frame for each owned element until the element is removed. `AllocateAndCopy()` does not copy owner-written fields - they are expected to be written fresh via `Sync()` each frame. Skipping `Sync()` leaves fields uninitialized, causing rendering artifacts.

## Adding New Collection Members

Use the **add-collection-member** skill for the 5-step checklist when adding new member pointers: struct declaration, Members() tuple, equality comparison, Update() load/save, and Spawn() initialization.

## See Also
- `/Projects/*/Source/Frame/Collections/` - Game-specific collection implementations
- `/Engine/Source/Frame/FrameBase.h` - Frame versioning and base classes
