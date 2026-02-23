# /Engine/Source/Frame/Collections/

Template utilities and concrete collections for managing dynamically-allocated Structure-of-Arrays (SOA) data with efficient memory management, serialization, and GPU rendering support.

## Architecture Overview

The collection system provides a layered template library for SOA memory management:

1. **Collection.h** - Core template infrastructure with memory alignment, allocation, element manipulation, serialization, and base classes
2. **Concrete Collections** - Engine-level collections for lights, effects, audio, and physics, each with self-contained GPU pipeline and buffer management

## Core Templates (Collection.h)

**Collection<T, FLAGS>** - Base class using CRTP providing count, capacity, and contiguous data buffer. Optional `CollectionFlags::kIdToIndex` enables ID-to-index mapping for stable external references.

**TypeRegistry<TType>** - Mixin for static type configuration sharing (textures, colors, etc.) across collection instances. Automatically triggers lazy texture chunk loading via `RequestTextureChunkLoad()` for types with a `crc` member (uses `requires` expression to detect at compile time).

**ControllerTypeRegistry<T, TControllerType>** - Mixin for keyframe animation support with time-based property interpolation. Default TControllerType uses `ControllerKeyframe` with visible/lighting area/intensity and rotation. Collections with custom keyframes (e.g., Puffs with `PuffKeyframe`) specify their own controller type and provide a corresponding `InterpolatePuffKeyframes()` function.

**Template Helpers** - Functions for allocation (`Allocate`, `AllocateAndAssign` which reuses existing buffer when capacity is sufficient), element operations (`SwapElement`, `DestroyElement`, `AddElement`), indexable collections (`GrowPairedCollections`, `AddIndexableElement`, `RemoveIndexableElement`), and serialization (`CollectionCrc`, `CollectionWrite`, `CollectionRead`). UUID generation via `uuid_t::Generate(FramePostRenderBase&)` uses the per-Frame counter in FramePostRenderBase, accessed through `rFrame.postRender`.

## GPU Pipeline and Buffer Pattern

Each renderable collection owns its GPU pipeline and buffer lifecycle directly in its `.cpp` file, using static `constexpr kName`/`kCrc` identifiers declared in the header. The pattern is:

- **`GraphicsResources()`**: Creates dynamic storage buffers via `BufferManager::CreateDynamicBuffer()` and registers pipelines via `PipelineManager::Create*()` methods. Pipelines and buffers are keyed by the collection's `kCrc`.
- **Three-Phase Render Pipeline**: Collections implement `BeginRender()`, `Render()`, and `EndRender()` static methods:
  - **`BeginRender(iCommandBuffer, rRenderInterpolates, rActiveCoords)`**: Sums counts across all active frames to compute total capacity, resizes GPU buffers if needed via `BufferManager::ResizeDynamicBufferIfNeeded()`, and resets a file-scope render count to zero.
  - **`Render(rFrameInterpolate, iCommandBuffer)`**: Writes GPU data for a single frame's collection at the current offset (accumulated render count), then advances the count. Called once per active frame.
  - **`EndRender(iCommandBuffer)`**: Writes the accumulated render count to the indirect draw buffer via `WriteIndirectBuffer()`.
- **Buffer Bounds Validation**: Collections retrieve GPU buffers via `GetDynamicStorageBuffer<T>()` which returns both the mapped pointer and buffer capacity. Render methods assert that the write count does not exceed the buffer capacity before writing.

## Engine Collections

| Collection | Rendering | ID-Indexed | Purpose |
|------------|-----------|------------|---------|
| **AreaLights** | Lighting + VisibleLights | Yes | Quad-based area lights for projectiles/effects |
| **Billboards** | Billboards | Yes | Screen-space UI indicators with offscreen handling |
| **PointLights** | AxisAlignedLighting + VisibleLights | Yes | Circular point lights with keyframe animation |
| **Puffs** | SmokeAxisAligned | No | Fire-and-forget smoke puffs with custom puff keyframe animation |
| **SmokeTrails** | Smoke | Yes | Externally-managed smoke trails with frame-rate-independent exponential position smoothing via `ExponentialInterpolant`. Trail geometry is built from current-to-smoothed position. Render iterates `idToIndexMap` to look up/create smoothed positions by trail ID in an `unordered_map`, and cleans up stale IDs with `std::erase_if`. Render takes `uiFrameId` for per-frame render state keying. `ResetRenderState()` clears cached positions on world reset |
| **HexShields** | HexShields + HexShieldsLighting | Yes | Geodesic shield meshes with directional damage |
| **Explosions** | None | No | Composite effects spawning lights, puffs, smoke trails, wind radials, and GPU particles. `ExplosionType` configures controller type indices for primary/secondary lights, puffs, smoke trails, and wind; particle physics/color; secondary explosion count; and trail parameters. `SpawnInfo` provides per-instance scaling (light, size, smoke, time percentages), trail/particle counts and angles, color flags (kYellow, kRed), and self-destroy flag. SmokeTrails simulate gravity during Interpolate::Update. Fire-and-forget radial wind via `WindRadialsPostRender::AddControlled()`. GPU particle spawning is guarded by `FrameFlags::kRecalculated` to avoid duplicate particles during frame recalculation |
| **WindTrails** | WindDeposit | Yes | Directional wind simulation input quads (Sync pattern, owner-managed). Renders oriented quads from previous-to-current position with configurable width and length multiplier. Render iterates `idToIndexMap` to look up/create previous positions by trail ID in an `unordered_map`, and cleans up stale IDs with `std::erase_if`. Render takes `uiFrameId` for per-frame render state keying. `ResetRenderState()` clears cached positions on world reset |
| **WindRadials** | WindDepositAxisAligned | No | Radial wind simulation input quads (Controller pattern, fire-and-forget with auto-destroy). Uses `WindRadialControllerType` with `WindRadialKeyframe` (intensity + size) for animated expansion. Axis-aligned rendering via `BuildAxisAlignedQuad()` with radial flag in params.w |
| **Pushers** | None | Yes | Physics force fields with player-centered zone-based spatial acceleration for `ApplyPush()` queries with flag-based include/exclude filtering |
| **Sounds** | None | Yes | 3D spatial audio sources |

## Dual-Phase Pattern

All collections follow the Interpolate/PostRender dual-phase pattern:
- **Interpolate struct**: Rendering state, position/transform data, optional controller animation. Static methods: `Register()`, `GraphicsResources()`, `AllocateAndCopy()`, `Update()`, `BeginRender()`, `Render()`, `EndRender()`
- **PostRender struct**: Logic operations (Add, Remove, Spawn, Destroy), ID tracking. Static methods: `AllocateAndCopy()`, `Update()`, `PreCollision()`, `PostCollision()`, `AreaDamage()`, `Transfer()`, `Destroy()`, `Spawn()`

## Sync Pattern

Collections with external ownership use `SyncData` structs and `Sync()` methods to encapsulate writes, enabling parent collections to update child state without exposing internal details. Used by AreaLights, Billboards, PointLights, Pushers, Sounds, SmokeTrails, HexShields, and WindTrails.

**Critical:** Owners MUST call `Sync()` every frame for each owned element until the element is removed. `AllocateAndCopy()` copies all Sync-written fields from the previous frame so that render interpolates always have valid data. Owners still overwrite these fields via `Sync()` each frame with current values.

## Controller Pattern (Fire-and-Forget)

Collections supporting keyframe animation can also be spawned as fire-and-forget via `AddControlled()`. These elements are not owned by any parent - they self-manage their state via controller keyframe interpolation in `Update()` and auto-destroy when the animation expires (if `bDestroysSelf` is set). Used by PointLights, Puffs, and WindRadials. WindRadials use `WindRadialControllerType` with `WindRadialKeyframe` (intensity + size) for radial wind effects spawned by explosions.

## Render-Only State Pattern

Collections that need previous-position tracking for rendering (direction computation, trail drawing) but don't need that data in the serialized frame state use file-scope static `unordered_map<uint16_t, RenderState>` keyed by frame ID. Each per-frame render state contains an `unordered_map` keyed by trail ID (e.g., `smoke_trails_t`, `wind_trail_t`) mapping to cached positions. This keeps position history out of dual-buffered frame data, avoiding unnecessary copies and serialization. During `Render()`, collections iterate `idToIndexMap` to look up or create entries by trail ID, then clean up stale IDs (removed trails) with `std::erase_if`. `ResetRenderState()` clears the per-frame map on world reset. Used by SmokeTrails (smoothed positions) and WindTrails (previous positions). Both receive `uiFrameId` in their `Render()` calls (excluded from `InterpolateRenderTypes` and called separately in `RenderFrameMain()`).

## Adding New Collection Members

Use the **add-collection-member** skill for the 5-step checklist when adding new member pointers: struct declaration, Members() tuple, equality comparison, Update() load/save, and Spawn() initialization.

## See Also
- `/Projects/*/Source/Frame/Collections/` - Game-specific collection implementations
- `/Engine/Source/Frame/FrameBase.h` - Frame versioning and base classes
