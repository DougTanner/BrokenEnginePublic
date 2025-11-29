# /Projects/BrokenEngineSandbox/Source/Frame/Collections/

Game-specific object collections for space combat. Manages projectiles and enemies using phase-separated dynamic memory with Structure-of-Arrays layout.

## Architecture Overview

**Phase Separation**: Collections split into Interpolate and PostRender structures for strict separation between rendering state and logic state, enabling deterministic replay.

**Memory Layout**: Structure of Arrays (SOA) with dynamic allocation - position arrays, velocity arrays, etc. allocated as single contiguous buffers. Enables SIMD optimization and cache-friendly iteration.

**Lifecycle Management**: Objects created via spawn requests, updated through frame phases, destroyed when flagged. Dynamic capacity growth handles variable object counts.

## Core Collections

### Blasters.h/cpp

Fast-moving energy projectile system with phase-separated dynamic memory management, shared configuration types, and decoupled collision detection.

**Purpose**: Manages rapid-fire projectiles with strict separation between rendering state and logic state, using shared BlasterType configuration for memory efficiency and engine Colliders collection for collision detection.

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
- Inherits from `engine::Collection<BlastersInterpolate>` using CRTP for consistent count/capacity/pData interface
- Dynamically allocated arrays: type indices (uint8_t), positions (XMVECTOR), area light IDs (engine::area_lights_t), collider IDs (engine::collider_t)
- Static Update() integrates velocity into position from previous frame state and delta time
- Static Sync() synchronizes positions to area lights and colliders using ColliderPostRender::UpdatePosition()
- Retrieves blaster dimensions from BlasterType via type index to create velocity-aligned quads
- Uses common::CalculateArea() to generate area light corner positions based on velocity direction
- Accesses area lights via idToIndexMap for position updates using strong-typed IDs
- `Members()` method returns `std::tie()` of member pointers for engine template functions
- Full serialization support via equality comparison and collection base methods

**BlastersPostRender Structure**:
- Inherits from `engine::Collection<BlastersPostRender>` using CRTP for consistent count/capacity/pData interface
- Dynamically allocated flag (BlasterFlags_t) and velocity (XMVECTOR) arrays
- Static Update() copies flags and velocities from previous frame
- Static Collide() queries collision system via engine::CollidersPostRender::HasCollision() and GetCollisions(), performs terrain collision detection, marks colliding/out-of-bounds blasters for destruction
- Static Spawn() creates new blasters with automatic capacity growth, stores type index, creates area light, registers collider via engine::CollidersPostRender::Add()
- Static Destroy() removes flagged blasters, unregisters colliders via engine::CollidersPostRender::Remove(), removes area lights, uses `engine::SwapElement()` for efficient unordered removal
- `Members()` method returns `std::tie()` of member pointers for engine template functions
- Full serialization support via equality comparison and collection base methods

**Blaster Type Registration Flow**:
- BlasterType instances store shared configuration: texture CRC, visual size, light area/intensity
- RegisterType() automatically creates AreaLightType with matching CRC and lighting configuration, stores returned type index in BlasterType
- Spawn() receives type index, retrieves full configuration via GetType(), passes type index to area lights Add() method for efficient lookup
- Update() uses type index to retrieve dimensions for area light position calculations
- Single type registration creates one area light type used by all instances of that blaster configuration
- Reduces memory overhead when firing many instances of the same blaster type

**Memory Management**:
- Single contiguous allocation via `common::AlignedUniquePtr<std::byte>` with 64-byte alignment
- Update methods use `engine::ReallocateAndCopyMetadata()` to copy metadata and reallocate buffers
- Spawn method uses `engine::GrowPairedCollections()` for automatic capacity growth and `engine::IncrementCountsAndGetSpawnIndex()` for index calculation
- Spawn stores type index, creates area light with type's pre-registered index, registers collider
- Destroy unregisters colliders before removing area lights and swapping elements
- Destroy method uses `engine::SwapElement()` for O(1) removal without preserving order

**Collision Integration**:
- Blasters register with category kBlaster, mask targeting spaceships, and kDestroyOnCollide flag
- Spawn creates collider with blaster category, collision mask, and destroy-on-collide flag
- Update synchronizes collider position via CollidersPostRender::UpdatePosition()
- Collide queries collision system and marks blasters for destruction on any collision
- kDestroyOnCollide flag prevents blasters from hitting multiple targets in same frame
- Destroy unregisters collider via CollidersPostRender::Remove() before cleanup

### Missiles.h/cpp

Placeholder structure for future missile system implementation.

**Purpose**: Reserved for guided missile functionality with homing behavior.

**Current State**: Contains only Missiles struct with capacity constant (kiMax = 256). Full implementation commented out in `#if 0` block showing future phase-separated architecture with MissilesInterpolate and MissilesPostRender.

### Spaceships.h/cpp

Enemy spacecraft system with phase-separated dynamic memory management for AI behavior, weapons, health, and decoupled collision detection.

**Purpose**: Manages AI-controlled enemies with strict separation between rendering state and logic state, using engine Colliders collection for collision detection.

**Architecture**: Two independent structures with dynamic memory allocation:
- **SpaceshipsInterpolate**: Inherits from both `engine::Collection` and `engine::Renderable` mixin for position, direction, and destroyed time data for rendering with GPU pipeline support
- **SpaceshipsPostRender**: Inherits from `engine::Collection` for velocity, health, AI state, and weapon data for logic
- Each structure independently tracks count and capacity for proper serialization

**SpaceshipFlags**: Enum class defining AI behavior flags (flee player, exploding, return to island center) with typesafe flags wrapper.

**SpaceshipsInterpolate Structure**:
- Inherits from `engine::Collection<SpaceshipsInterpolate>` for count/capacity/pData interface
- Inherits from `engine::Renderable<SpaceshipsInterpolate, RenderableFlags::kGltfShadow, kGltfCrc, kModelCrc>` for GPU buffer and pipeline management (layout size inferred from flags)
- Dynamically allocated arrays: positions (XMVECTOR), directions (XMVECTOR), destroyed times (float), collider IDs (engine::collider_t)
- Static AllocateGraphicsResources() calls inherited AllocateGltfPipelines() to create main and shadow pipelines
- Static Update() integrates velocity into position and rotates direction using previous frame state and delta time
- Static Sync() synchronizes positions to colliders using CollidersPostRender::UpdatePosition()
- Instance Render() calls inherited ResizeAndUpdatePipelines() for dynamic buffer management, then submits GPU rendering commands with frustum culling and death shrink effects
- `Members()` method returns `std::tie()` of member pointers for engine template functions
- Full serialization support via equality comparison and collection base methods

**SpaceshipsPostRender Structure**:
- Inherits from `engine::Collection<SpaceshipsPostRender>` using CRTP for consistent count/capacity/pData interface
- Dynamically allocated arrays for flags (SpaceshipFlags_t), velocities (XMVECTOR), rotation (float), health (float), freeze times (float), explosion timers (float), weapon spawn timing (float), and blaster spawn counts (int32_t)
- Static Update() processes AI logic, weapon firing, and physics using previous frame state and delta time
- Static Collide() queries collision system via engine::CollidersPostRender::GetCollisions(), applies damage from colliding blasters, marks destroyed spaceships
- Static Spawn() creates new spaceships with automatic capacity growth, registers collider via engine::CollidersPostRender::Add() with kTakeDamage behavior
- Static Destroy() removes destroyed spaceships, unregisters colliders via engine::CollidersPostRender::Remove()
- `Members()` method returns `std::tie()` of member pointers for engine template functions
- Full serialization support via equality comparison and collection base methods

**Collision Integration**:
- Spaceships register with category kSpaceship and mask allowing collisions with player and blasters
- Spawn creates collider with spaceship category and collision mask
- Sync synchronizes collider position via CollidersPostRender::UpdatePosition()
- Collide queries collision system, applies damage from blaster collisions, marks destroyed spaceships
- Destroy unregisters collider via CollidersPostRender::Remove() before cleanup

**Memory Management**:
- Single contiguous allocation via `common::AlignedUniquePtr<std::byte>` with 64-byte alignment
- Update methods use `engine::ReallocateAndCopyMetadata()` to copy metadata and reallocate buffers
- Spawn method uses `engine::GrowPairedCollections()` for automatic capacity growth and `engine::IncrementCountsAndGetSpawnIndex()` for index calculation

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
- Resizes buffer and updates descriptors using update-after-bind feature
- No command buffer re-recording required due to VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
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
- **`engine::GrowPairedCollections()`**: Used in Spawn() methods for automatic capacity growth of paired Interpolate/PostRender collections. Internally uses GrowCapacityWithCopy().
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

### Tuple-Based Member Lists

Structures provide a `Members()` method returning `std::tie()` of their member pointers. This tuple-based approach enables type-safe member list passing to engine template functions:
- `Members()` method returns `std::tie(pVecPositions, pVecVelocities, ...)` of all SOA member pointers
- Engine template functions accept tuples directly and use `std::apply()` internally for fold expression operations
- Collection serialization methods call `engine::CollectionCrc()`, `engine::CollectionWrite()`, `engine::CollectionRead()` with `.Members()`
- Collection Update/Spawn methods call `engine::ReallocateAndCopyMetadata()`, `engine::GrowPairedCollections()`, `engine::SwapElement()` with `.Members()`
- Fold expressions over member pointer types calculate buffer sizes internally
- Compile-time type checking ensures consistency across CRC, serialization, and allocation
- Equality operators use `bEqual &= CompareCountAndCapacity(rOther);` pattern to validate count/capacity match

### Adding New Members to Collections

When adding new members to game collection structures, follow the 5-step pattern documented in the **add-collection-member** skill. Use `/add-collection-member` or invoke the skill to see the complete checklist with examples.

**Quick Summary**:
1. Add member pointer to struct and update `Members()` method to include it in `std::tie()`
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
