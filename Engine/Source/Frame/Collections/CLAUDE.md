# /Engine/Source/Frame/Collections/

Template utilities and concrete collections for managing dynamically-allocated Structure-of-Arrays (SOA) data with efficient memory management, serialization, and GPU rendering support.

## Architecture Overview

The collection system provides a layered template library for SOA memory management:

1. **Collection.h** - Core template infrastructure with memory alignment, allocation, element manipulation, serialization, and base classes
2. **Concrete Collections** - Engine-level collections for lights, effects, audio, and physics, each with self-contained GPU pipeline and buffer management

## Core Templates (Collection.h)

**Collection<T, FLAGS>** - CRTP base class managing count, capacity, and a single contiguous data buffer. Optional `CollectionFlags::kIdToIndex` enables stable external references via ID-to-index mapping with strong-typed `id_t<T>` wrappers that prevent cross-collection ID misuse.

**TypeRegistry<TType>** - Mixin for static type configuration sharing across collection instances. Automatically triggers lazy texture chunk loading for types with a `crc` member.

**ControllerTypeRegistry<T, TControllerType>** - Mixin for keyframe animation with time-based property interpolation. Collections with custom keyframes (e.g., Puffs, WindRadials) specify their own controller type and interpolation function.

**Template Helpers** - Functions for allocation (with capacity-based buffer reuse), paired Interpolate/PostRender element manipulation (swap-and-pop removal, growth), indexable element management (ID generation via `AddIndexableElement` or reuse of existing IDs via `AddIndexableElementWithId` for cross-frame transitions), and deterministic serialization with CRC validation.

## GPU Pipeline and Buffer Pattern

Each renderable collection owns its GPU pipeline and buffer lifecycle, keyed by a static `kCrc`. The three-phase render pipeline (`BeginRender`/`Render`/`EndRender`) sums capacity across active frames, resizes GPU buffers if needed, writes per-frame data at accumulated offsets, and finalizes with an indirect draw buffer write. Buffer bounds are validated via assert.

## Engine Collections

| Collection | ID-Indexed | Purpose |
|------------|------------|---------|
| **AreaLights** | Yes | Quad-based area lights for projectiles/effects (Sync pattern) |
| **Billboards** | Yes | Screen-space UI indicators with offscreen handling (Sync pattern) |
| **PointLights** | Yes | Circular point lights with optional keyframe animation (Sync + Controller patterns) |
| **Puffs** | No | Fire-and-forget smoke puffs with custom keyframe animation (Controller pattern) |
| **SmokeTrails** | Yes | Externally-managed smoke trails with frame-rate-independent exponential position smoothing for trail geometry (Sync pattern). Supports ID reuse for continuous rendering across grid cell transfers |
| **HexShields** | Yes | Geodesic shield meshes with directional damage visualization (Sync pattern) |
| **Explosions** | No | Composite effects that spawn lights, puffs, smoke trails, wind radials, and GPU particles. Applies gravity to owned smoke trails via Sync during Update. GPU particle spawning guarded by `kRecalculated` flag |
| **WindTrails** | Yes | Directional wind simulation input quads rendered from previous-to-current position (Sync pattern) |
| **WindRadials** | No | Radial wind simulation input quads with animated expansion (Controller pattern) |
| **Pushers** | Yes | Physics force fields with zone-based spatial acceleration for push queries with flag-based filtering (Sync pattern) |
| **Sounds** | Yes | 3D spatial audio sources (Sync pattern) |

## Sync Pattern

Collections with external ownership use `SyncData` structs and `Sync()` methods to encapsulate writes, enabling parent collections to update child state without exposing internal details.

**Critical:** Owners MUST call `Sync()` every frame for each owned element until the element is removed. `AllocateAndCopy()` copies all Sync-written fields from the previous frame so that render interpolates always have valid data.

## Controller Pattern (Fire-and-Forget)

Collections supporting keyframe animation can be spawned as fire-and-forget via `AddControlled()`. These elements self-manage via controller keyframe interpolation in `Update()` and auto-destroy when the animation expires. Used by PointLights, Puffs, and WindRadials.

## Render-Only State Pattern

SmokeTrails and WindTrails need previous-position tracking for rendering but not in serialized frame state. They use a single global render state (keyed by globally unique UUIDs) kept in file-scope statics, keeping position history out of dual-buffered frame data. Stale entries are pruned in BeginRender by checking all active frames. Both are excluded from the standard render type list and called separately with a per-frame ID parameter.

## Adding New Collection Members

Use the **add-collection-member** skill for the 5-step checklist when adding new member pointers.

## See Also
- `/Projects/*/Source/Frame/Collections/` - Game-specific collection implementations
- `/Engine/Source/Frame/FrameBase.h` - Frame versioning and base classes
