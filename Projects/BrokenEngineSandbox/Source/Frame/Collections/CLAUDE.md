# /Projects/BrokenEngineSandbox/Source/Frame/Collections/

Game-specific object collections for space combat. Manages projectiles and enemies using phase-separated dynamic memory with Structure-of-Arrays layout.

## Architecture Overview

**Phase Separation**: Collections split into Interpolate and PostRender structures for strict separation between rendering state and logic state, enabling deterministic replay.

**Memory Layout**: Structure of Arrays (SOA) with dynamic allocation - position arrays, velocity arrays, etc. allocated as single contiguous buffers. Enables SIMD optimization and cache-friendly iteration.

**Lifecycle Management**: Objects created via spawn requests, updated through frame phases, destroyed when flagged. Dynamic capacity growth handles variable object counts.

## Core Collections

### Blasters.h/cpp

Fast-moving energy projectile system with phase-separated dynamic memory management and shared configuration types.

**Purpose**: Manages rapid-fire projectiles with strict separation between rendering state and logic state, using shared BlasterType configuration for memory efficiency.

**BlasterType Structure**:
- Stores shared configuration for all blaster instances (texture CRC, visual/lighting sizes, light intensities)
- Static registry maintained in `Blasters.cpp` (sBlasterTypes vector)
- Static RegisterType() method registers new blaster type and automatically creates corresponding AreaLightType
- Static GetType() method retrieves BlasterType by index
- Area light type registration returns index stored in BlasterType::uiAreaLightTypeIndex for deferred lookup
- Player registers blaster types in PlayerPostRender constructor, storing index in PlayerPostRender::suiBlasterTypeIndex

**Architecture**: Two independent structures inheriting from `engine::Collection` with dynamic memory allocation:
- **BlastersInterpolate**: Position, area light IDs, and type indices for rendering
- **BlastersPostRender**: Velocity and flag data for logic
- Each structure independently tracks count and capacity for proper serialization

**BlasterFlags**: Enum class defining blaster state flags with typesafe flags wrapper. Currently contains only the destroy flag for marking blasters to be removed.

**BlastersInterpolate Structure**:
- Inherits from `engine::Collection<BlastersInterpolate, kiBlastersInterpolateVersion>` using CRTP for consistent count/capacity/pData interface
- Dynamically allocated position (XMVECTOR), area light ID, and type index (uint8_t) arrays
- Static AllocateAndCopy() copies metadata and reallocates buffer using ReallocateAndCopyMetadata
- Static Update() integrates velocity into position and syncs area light positions using previous frame state and delta time
- Retrieves blaster dimensions from BlasterType via type index to create velocity-aligned quads
- Uses common::CalculateArea() to generate area light corner positions based on velocity direction
- Accesses area lights via idToIndexMap for position updates using strong-typed IDs
- Macro-based member list (BLASTERS_INTERPOLATE_LIST) enables engine template functions for serialization
- Full serialization support via equality comparison and Write/Read member functions

**BlastersPostRender Structure**:
- Inherits from `engine::Collection<BlastersPostRender, kiBlastersPostRenderVersion>` using CRTP for consistent count/capacity/pData interface
- Dynamically allocated flag (BlasterFlags_t) and velocity (XMVECTOR) arrays
- Static RegisterType() and GetType() methods manage BlasterType registry
- Static AllocateAndCopy() copies metadata and reallocates buffer using ReallocateAndCopyMetadata
- Static Update() copies flags and velocities from previous frame
- Static Collide() performs terrain collision detection and global area boundary checking, marks out-of-bounds blasters for destruction
- Static Spawn() creates new blasters with automatic capacity growth using helper functions, stores type index in interpolate structure, creates area light with type's registered index
- Static Destroy() removes flagged blasters using `engine::SwapElement()` for efficient unordered removal
- Macro-based member list (BLASTERS_POST_RENDER_LIST) enables engine template functions for serialization
- Full serialization support via equality comparison and Write/Read member functions

**Blaster Type Registration Flow**:
- BlasterType instances store shared configuration: texture CRC, visual size, light area/intensity
- RegisterType() automatically creates AreaLightType with matching CRC and lighting configuration, stores returned type index in BlasterType
- Spawn() receives type index, retrieves full configuration via GetType(), passes type index to area lights Add() method for efficient lookup
- Update() uses type index to retrieve dimensions for area light position calculations
- Single type registration creates one area light type used by all instances of that blaster configuration
- Reduces memory overhead when firing many instances of the same blaster type

**Memory Management**:
- Single contiguous allocation via `common::AlignedUniquePtr<std::byte>` with 64-byte alignment
- AllocateAndCopy methods use `engine::ReallocateAndCopyMetadata()` to copy metadata and reallocate buffers before Update() phase
- Update methods check for null data with early-exit pattern
- Spawn method uses `engine::CalculateGrowthCapacity()` to check for growth, `engine::GrowCapacityWithCopy()` for capacity expansion, and `engine::IncrementCountsAndGetSpawnIndex()` for index calculation
- Spawn stores type index in interpolate structure and creates area light with type's pre-registered index
- Destroy removes area lights via `rFrame.postRender.areaLights.Remove(rFrame, areaLightId)` before swapping elements
- Destroy method uses `engine::SwapElement()` for O(1) removal without preserving order
- Deserialization uses `engine::AllocateAndRead()` helper for allocation and pointer setup

### Missiles.h/cpp

Placeholder structure for future missile system implementation.

**Purpose**: Reserved for guided missile functionality with homing behavior.

**Current State**: Contains only Missiles struct with capacity constant (kiMax = 256). Full implementation commented out in `#if 0` block showing future phase-separated architecture with MissilesInterpolate and MissilesPostRender.

### Spaceships.h/cpp

Enemy spacecraft system with phase-separated dynamic memory management for AI behavior, weapons, and health.

**Purpose**: Manages AI-controlled enemies with strict separation between rendering state and logic state.

**Architecture**: Two independent structures with dynamic memory allocation:
- **SpaceshipsInterpolate**: Inherits from both `engine::Collection` and `engine::Renderable` mixin for position, direction, and destroyed time data for rendering with GPU pipeline support
- **SpaceshipsPostRender**: Inherits from `engine::Collection` for velocity, health, AI state, and weapon data for logic
- Each structure independently tracks count and capacity for proper serialization

**SpaceshipFlags**: Enum class defining AI behavior flags (flee player, exploding, return to island center) with typesafe flags wrapper.

**SpaceshipsInterpolate Structure**:
- Inherits from `engine::Collection<SpaceshipsInterpolate>` for count/capacity/pData interface
- Inherits from `engine::Renderable<SpaceshipsInterpolate, sizeof(GltfLayout), kGltfCrc, kModelCrc>` for GPU buffer and pipeline management (shadows enabled by default)
- Dynamically allocated position (XMVECTOR), direction (XMVECTOR), and destroyed time (float) arrays
- Static AllocateGraphicsResources() calls inherited AllocateGltfPipelines() to create main and shadow pipelines
- Static AllocateAndCopy() copies metadata and reallocates buffer using ReallocateAndCopyMetadata
- Static Update() integrates velocity into position and rotates direction using previous frame state and delta time with early-exit for null data
- Instance Render() calls inherited ResizeAndUpdatePipelines() for dynamic buffer management, then submits GPU rendering commands with frustum culling and death shrink effects
- Macro-based member list (SPACESHIPS_INTERPOLATE_LIST) enables engine template functions for serialization
- Full serialization support via equality comparison and Write/Read member functions

**SpaceshipsPostRender Structure**:
- Inherits from `engine::Collection<SpaceshipsPostRender, kiSpaceshipsPostRenderVersion>` using CRTP for consistent count/capacity/pData interface
- Dynamically allocated arrays for flags (SpaceshipFlags_t), velocities (XMVECTOR), rotation (float), health (float), freeze times (float), explosion timers (float), weapon spawn timing (float), and blaster spawn counts (int32_t)
- Static AllocateAndCopy() copies metadata and reallocates buffer using ReallocateAndCopyMetadata
- Static Update() processes AI logic, weapon firing, and physics using previous frame state and delta time
- Update() uses early-exit pattern for null data
- Static Collide() handles spaceship collision detection
- Static Spawn() creates new spaceships with automatic capacity growth using helper functions
- Static Destroy() removes destroyed spaceships
- Macro-based member list (SPACESHIPS_POST_RENDER_LIST) enables engine template functions for serialization
- Full serialization support via equality comparison and Write/Read member functions

**Memory Management**:
- Single contiguous allocation via `common::AlignedUniquePtr<std::byte>` with 64-byte alignment
- AllocateAndCopy methods use `engine::ReallocateAndCopyMetadata()` to copy metadata and reallocate buffers before Update() phase. Buffer size calculated internally via fold expressions over member pointer types.
- Update methods check for null data with early-exit pattern
- Spawn method uses `engine::CalculateGrowthCapacity()` to check for growth, `engine::GrowCapacityWithCopy()` for capacity expansion, and `engine::IncrementCountsAndGetSpawnIndex()` for index calculation. Buffer size calculated internally via fold expressions over member pointer types.
- Deserialization uses `engine::AllocateAndRead()` helper for allocation and pointer setup. Buffer size calculated internally via fold expressions over member pointer types.

**Rendering**:
- Pre-rotation matrix aligns model coordinate system with game world
- Frustum culling based on camera visible area
- Death shrink effect using pow() function on destroy time ratio
- Transformation matrix combines scaling, pre-rotation, yaw rotation, and translation
- Writes GltfLayout structures to mapped GPU storage buffer
- Submits draw commands for both main rendering and shadow passes

**Dynamic Buffer Resizing**:
- Handled by inherited Renderable::ResizeAndUpdatePipelines() method
- Automatically detects when collection capacity exceeds storage buffer size
- Resizes buffer, updates descriptors, re-records secondary command buffers
- Sets kNeedsRerecord flag for deferred primary command buffer re-recording
- Enables runtime capacity growth without frame stalls or visible artifacts

**Design Pattern**: Structure of Arrays layout with dynamic allocation provides cache-friendly iteration while supporting variable enemy counts. Phase separation ensures rendering state (positions, directions, destroyed times) is independent from logic state (velocities, health, AI flags, weapon state). The AllocateAndCopy phase runs before Update() to handle metadata copying and buffer reallocation, enabling Update() methods to safely reference collection metadata across collections.

## Common Patterns

### SOA Layout with Dynamic Allocation

Collections organize data by access pattern:
- **Interpolate structures**: Rendering state (positions, directions) updated during interpolation phase
- **PostRender structures**: Logic state (velocities, health, flags) updated during PostRender phase
- Single contiguous allocation per structure with 64-byte alignment for cache efficiency
- RAII cleanup via `common::AlignedUniquePtr<std::byte>`

### Memory Management Helpers

Core patterns simplify dynamic allocation:
- **`engine::ReallocateAndCopyMetadata()`**: Used in AllocateAndCopy() methods to copy metadata and reallocate buffers before Update() phase. Buffer size calculated via fold expressions over member pointer types.
- **`engine::CalculateGrowthCapacity()`**: Used in Spawn() methods to check if capacity growth is needed. Returns new capacity (2 * capacity + 1) if growth needed, 0 otherwise.
- **`engine::GrowCapacityWithCopy()`**: Used in Spawn() methods when growth is needed. Grows capacity while preserving existing data. Buffer size calculated via fold expressions over member pointer types.
- **`engine::IncrementCountsAndGetSpawnIndex()`**: Used in Spawn() methods to increment counts for paired collections and return spawn index.
- **`engine::AllocateAndRead()`**: Used in deserialization, allocates buffers and sets up member pointers from stream. Buffer size calculated via fold expressions over member pointer types.

### Serialization Support

Each structure provides full serialization:
- Equality comparison for change detection (uses `bEqual &= CompareCountAndCapacity(rOther);` pattern)
- Write() and Read() member functions call engine template functions for serialization
- `engine::CollectionCrc()` template used for replay validation CRCs
- `engine::WriteCollection()` template used for stream output
- `engine::ReadCollection()` template used for stream input
- Serializes count, capacity, and only active elements to minimize file size

### Macro-Based Member Lists

Structures use macros to define member lists that are passed to engine template functions:
- Example: `#define BLASTERS_INTERPOLATE_LIST(a) a.pVecPositions`
- Example: `#define SPACESHIPS_POST_RENDER_LIST(a) a.pFlags, a.pVecVelocities, a.pfDeltaRotations, a.pfHealths, a.pfFreezeTimes, a.pfDestroyedExplosionTimes, a.pfNextBlasterSpawnTimes, a.piBlasterSpawns`
- Collection Write/Read member functions pass collection and macro to engine template functions
- `engine::CollectionCrc()` for CRC calculation
- `engine::WriteCollection()` for serialization
- `engine::ReadCollection()` for deserialization
- Collection Update/Spawn methods pass macro to `engine::ReallocateIfCapacityChanged()`, `engine::GrowCapacityWithCopy()` for memory management
- Fold expressions over member pointer types calculate buffer sizes internally
- Ensures consistency across CRC, serialization, and allocation
- Equality operators use `bEqual &= CompareCountAndCapacity(rOther);` pattern to validate count/capacity match

### Adding New Members to Collections

When adding new members to game collection structures, follow the 5-step pattern documented in the **add-collection-member** skill. Use `/add-collection-member` or invoke the skill to see the complete checklist with examples.

**Quick Summary**:
1. Update macro list in header file
2. Add equality comparison in operator==()
3. Load member in Update() method
4. Save member in Update() method
5. Initialize member in Spawn() method

Missing any step will cause compilation errors, runtime crashes, or determinism failures. See the skill for detailed instructions and code examples.

## Integration with Engine

**Frame Integration**: Collections are members of FrameInterpolate and FramePostRender structures, participating in hierarchical version tracking and serialization.

**Update Flow**: Static Update() methods called from parent frame's update phases, receiving previous frame state and delta time for deterministic simulation.

## See Also
- Engine collections base: [../../../../../Engine/Source/Frame/Collections/CLAUDE.md](../../../../../Engine/Source/Frame/Collections/CLAUDE.md)
- Engine object pools: [../../../../../Engine/Source/Frame/Pools/CLAUDE.md](../../../../../Engine/Source/Frame/Pools/CLAUDE.md)
- Parent frame: [../CLAUDE.md](../CLAUDE.md)
