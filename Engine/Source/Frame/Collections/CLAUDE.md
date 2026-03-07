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

**Template Helpers** - Functions for allocation (with capacity-based buffer reuse), paired Interpolate/PostRender element manipulation (swap-and-pop removal, growth), indexable element management (`AddIndexableElement` generates new IDs via FramePostRender's UUID generator; `AddVisualIndexableElement` is the client-only variant that uses the separate `uiNextVisualUuid` counter for visual objects so their ID generation does not perturb the main UUID sequence; `AddIndexableElementWithId` reuses an existing ID for cross-cell transfer continuity), and deterministic serialization with CRC validation. `ServerCollectionCrc()` uses the `HasSharedMembers` concept to choose `SharedMembers()` (if defined) over `Members()` for computing cross-build CRCs, enabling collections with client-only fields to exclude them from server validation. `ServerCollectionElementCrc()` computes a per-element CRC using the same `SharedMembers`/`Members` selection, enabling per-element desync diagnosis via `MultiElementCrc()` which XORs CRCs across all member arrays for a single element index. `ServerCollectionRead()` deserializes server-format streams on the client: it allocates the full `Members()` buffer (zero-initialized so client-only fields default to safe values), then reads only `SharedMembers()` from the stream to match the server's write format.

Each collection provides a `ServerCompare()` method for desync diagnosis. Collections with client-only fields implement explicit per-field `BreakOnNotEqual` comparison on `SharedMembers()` only. Collections without client-only fields delegate to `operator==`. This enables precise field-level identification of differences between client and server state when CRC mismatches are detected.

## GPU Pipeline and Buffer Pattern

Each renderable collection owns its GPU pipeline and buffer lifecycle, keyed by a static `kCrc`. The three-phase render pipeline (`BeginRender`/`Render`/`EndRender`) sums capacity across active frames, resizes GPU buffers if needed, writes per-frame data at accumulated offsets, and finalizes with an indirect draw buffer write. Buffer bounds are validated via assert.

## Engine Collections

Collections are split between server-relevant (always compiled) and client-only (visual/audio, `#ifdef BT_CLIENT`). Each collection lives in its own subdirectory with a dedicated CLAUDE.md.

### Server Collections (always compiled)

| Collection | ID-Indexed | Purpose |
|------------|------------|---------|
| **[Explosions](Explosions/CLAUDE.md)** | No | Composite effects spawning lights, puffs, trails, wind radials, and GPU particles |
| **[Pushers](Pushers/CLAUDE.md)** | Yes | Physics force fields with zone-based spatial acceleration and flag-based filtering |

### Client-Only Collections (`#ifdef BT_CLIENT`)

| Collection | ID-Indexed | Purpose |
|------------|------------|---------|
| **[AreaLights](AreaLights/CLAUDE.md)** | Yes | Quad-based area lights for projectiles/effects (Sync pattern) |
| **[Billboards](Billboards/CLAUDE.md)** | Yes | Screen-space UI indicators with offscreen handling (Sync pattern) |
| **[PointLights](PointLights/CLAUDE.md)** | Yes | Circular point lights with optional keyframe animation (Sync + Controller patterns) |
| **[Puffs](Puffs/CLAUDE.md)** | No | Fire-and-forget smoke puffs with custom keyframe animation (Controller pattern) |
| **[SmokeTrails](SmokeTrails/CLAUDE.md)** | Yes | Externally-managed smoke trails with position smoothing and ID reuse for transfer continuity |
| **[HexShields](HexShields/CLAUDE.md)** | Yes | Geodesic shield meshes with directional damage visualization (Sync pattern) |
| **[WindTrails](WindTrails/CLAUDE.md)** | Yes | Directional wind simulation input quads (Sync pattern) |
| **[WindRadials](WindRadials/CLAUDE.md)** | No | Radial wind simulation input quads with animated expansion (Controller pattern) |
| **[Sounds](Sounds/CLAUDE.md)** | Yes | 3D spatial audio sources (Sync pattern) |

## Sync Pattern

Collections with external ownership use `SyncData` structs and `Sync()` methods to encapsulate writes, enabling parent collections to update child state without exposing internal details.

**Critical:** Owners MUST call `Sync()` every frame for each owned element until the element is removed. `AllocateAndCopy()` copies all Sync-written fields from the previous frame so that render interpolates always have valid data.

## Controller Pattern (Fire-and-Forget)

Collections supporting keyframe animation can be spawned as fire-and-forget via `AddControlled()`. These elements self-manage via controller keyframe interpolation in `Update()` and auto-destroy when the animation expires. Used by PointLights, Puffs, and WindRadials.

## Render-Only State Pattern

SmokeTrails and WindTrails need previous-position tracking for rendering but not in serialized frame state. They use a single global render state (keyed by globally unique UUIDs) kept in file-scope statics, keeping position history out of dual-buffered frame data. SmokeTrails prunes stale entries in BeginRender by checking all active frames for ID presence. Both are excluded from the standard render type list and called separately with a per-frame ID parameter.

## Extern Template Pattern

Every collection header declares `extern template struct Collection<T>` (and `Collection<T, CollectionFlags::kIdToIndex>` for indexable collections) after the struct definitions, and each corresponding `.cpp` provides the explicit instantiation. This eliminates redundant `Collection<T>` template instantiation across translation units that include the header, reducing compile times.

## File Splitting Pattern

All collections live in their own `{Name}/` subdirectory. When a collection's `.cpp` exceeds size guidelines, split the implementation across multiple `.cpp` files sharing a single `.h`, organized by responsibility (core, update, render). See [game collections CLAUDE.md](../../../../Projects/BrokenEngineSandbox/Source/Frame/Collections/CLAUDE.md) for the full convention.

## Adding New Collection Members

Use the **add-collection-member** skill for the 5-step checklist when adding new member pointers.

## See Also
- [Explosions/CLAUDE.md](Explosions/CLAUDE.md) - Composite effect orchestration
- [Pushers/CLAUDE.md](Pushers/CLAUDE.md) - Physics force fields
- [AreaLights/CLAUDE.md](AreaLights/CLAUDE.md) - Quad-based area lights
- [Billboards/CLAUDE.md](Billboards/CLAUDE.md) - Screen-space UI indicators
- [PointLights/CLAUDE.md](PointLights/CLAUDE.md) - Circular point lights with keyframes
- [Puffs/CLAUDE.md](Puffs/CLAUDE.md) - Fire-and-forget smoke puffs
- [SmokeTrails/CLAUDE.md](SmokeTrails/CLAUDE.md) - Externally-managed smoke trails
- [HexShields/CLAUDE.md](HexShields/CLAUDE.md) - Geodesic shield meshes
- [WindTrails/CLAUDE.md](WindTrails/CLAUDE.md) - Directional wind simulation input
- [WindRadials/CLAUDE.md](WindRadials/CLAUDE.md) - Radial wind simulation input
- [Sounds/CLAUDE.md](Sounds/CLAUDE.md) - 3D spatial audio sources
- `/Projects/*/Source/Frame/Collections/` - Game-specific collection implementations
- `/Engine/Source/Frame/FrameBase.h` - Frame versioning and base classes
