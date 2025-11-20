# /Projects/BrokenEngineSandbox/Source/Frame/Collections/

Game-specific object collections for space combat. Manages projectiles and enemies using phase-separated dynamic memory with Structure-of-Arrays layout.

## Architecture Overview

**Phase Separation**: Collections split into Interpolate and PostRender structures for strict separation between rendering state and logic state, enabling deterministic replay.

**Memory Layout**: Structure of Arrays (SOA) with dynamic allocation - position arrays, velocity arrays, etc. allocated as single contiguous buffers. Enables SIMD optimization and cache-friendly iteration.

**Lifecycle Management**: Objects created via spawn requests, updated through frame phases, destroyed when flagged. Dynamic capacity growth handles variable object counts.

## Core Collections

### Blasters.h/cpp

Fast-moving energy projectile system with phase-separated dynamic memory management.

**Purpose**: Manages rapid-fire projectiles with strict separation between rendering state and logic state.

**Architecture**: Two independent structures inheriting from `engine::Collection` with dynamic memory allocation:
- **BlastersInterpolate**: Position and area light data for rendering
- **BlastersPostRender**: Velocity and flag data for logic
- Each structure independently tracks count and capacity for proper serialization

**BlasterFlags**: Enum class defining blaster state (destroy flag) with typesafe flags wrapper.

**BlastersInterpolate Structure**:
- Inherits from `engine::Collection` for consistent count/capacity/pData interface
- Dynamically allocated position (XMVECTOR) and area light (area_light_t) arrays
- Static Update() integrates velocity into position using previous frame state and delta time
- Uses `engine::ReallocateIfCapacityChanged()` to handle capacity synchronization with previous frame
- Macro-based member list (BLASTERS_INTERPOLATE_LIST) enables engine template functions for serialization
- Full serialization support via equality comparison and Write/Read member functions

**BlastersPostRender Structure**:
- Inherits from `engine::Collection` for consistent count/capacity/pData interface
- Dynamically allocated flag (BlasterFlags_t) and velocity (XMVECTOR) arrays
- Static Update() copies flags and velocities from previous frame
- Static Collide() performs terrain collision detection and marks destroyed blasters
- Static Spawn() creates new blasters with automatic capacity growth using helper functions
- Static Destroy() removes flagged blasters using `engine::SwapElement()` for efficient unordered removal
- Macro-based member list (BLASTERS_POST_RENDER_LIST) enables engine template functions for serialization
- Full serialization support via equality comparison and Write/Read member functions

**Memory Management**:
- Single contiguous allocation via `common::AlignedUniquePtr<std::byte>` with 64-byte alignment
- Update methods use `engine::ReallocateIfCapacityChanged()` which handles both null data (returns false) and capacity changes (returns true)
- Spawn method uses `engine::CalculateGrowthCapacity()` to check for growth, `engine::GrowCapacityWithCopy()` for capacity expansion, and `engine::IncrementCountsAndGetSpawnIndex()` for index calculation
- Destroy method uses `engine::SwapElement()` for O(1) removal without preserving order
- Deserialization uses `engine::AllocateAndRead()` helper for allocation and pointer setup

### Missiles.h/cpp

Placeholder structure for future missile system implementation.

**Purpose**: Reserved for guided missile functionality with homing behavior.

**Current State**: Contains only Missiles struct with capacity constant (kiMax = 256). Full implementation commented out in `#if 0` block showing future phase-separated architecture with MissilesInterpolate and MissilesPostRender.

### Spaceships.h/cpp

Enemy spacecraft system with phase-separated dynamic memory management for AI behavior, weapons, and health.

**Purpose**: Manages AI-controlled enemies with strict separation between rendering state and logic state.

**Architecture**: Two independent structures inheriting from `engine::Collection` with dynamic memory allocation:
- **SpaceshipsInterpolate**: Position, direction, and destroyed time data for rendering
- **SpaceshipsPostRender**: Velocity, health, AI state, and weapon data for logic
- Each structure independently tracks count and capacity for proper serialization

**SpaceshipFlags**: Enum class defining AI behavior flags (flee player, exploding, return to island center) with typesafe flags wrapper.

**SpaceshipsInterpolate Structure**:
- Inherits from `engine::Collection` for consistent count/capacity/pData interface
- Dynamically allocated position (XMVECTOR), direction (XMVECTOR), and destroyed time (float) arrays
- Static Update() integrates velocity into position and rotates direction using previous frame state and delta time
- Update() uses pattern: `if (!engine::ReallocateIfCapacityChanged(...)) { return; }` to handle null data and capacity changes
- Instance Render() submits GPU rendering commands with frustum culling and death shrink effects
- Macro-based member list (SPACESHIPS_INTERPOLATE_LIST) enables engine template functions for serialization
- Full serialization support via equality comparison and Write/Read member functions

**SpaceshipsPostRender Structure**:
- Inherits from `engine::Collection` for consistent count/capacity/pData interface
- Dynamically allocated arrays for flags (SpaceshipFlags_t), velocities (XMVECTOR), rotation (float), health (float), freeze times (float), explosion timers (float), weapon spawn timing (float), and blaster spawn counts (int32_t)
- Static Update() processes AI logic, weapon firing, and physics using previous frame state and delta time
- Update() uses pattern: `if (!engine::ReallocateIfCapacityChanged(...)) { return; }` to handle null data and capacity changes
- Static Collide() handles spaceship collision detection
- Static Spawn() creates new spaceships with automatic capacity growth using helper functions
- Static Destroy() removes destroyed spaceships
- Macro-based member list (SPACESHIPS_POST_RENDER_LIST) enables engine template functions for serialization
- Full serialization support via equality comparison and Write/Read member functions

**Memory Management**:
- Single contiguous allocation via `common::AlignedUniquePtr<std::byte>` with 64-byte alignment
- Update methods use `engine::ReallocateIfCapacityChanged()` which handles both null data (returns false) and capacity changes (returns true). Buffer size calculated internally via fold expressions over member pointer types.
- Spawn method uses `engine::CalculateGrowthCapacity()` to check for growth, `engine::GrowCapacityWithCopy()` for capacity expansion, and `engine::IncrementCountsAndGetSpawnIndex()` for index calculation. Buffer size calculated internally via fold expressions over member pointer types.
- Deserialization uses `engine::AllocateAndRead()` helper for allocation and pointer setup. Buffer size calculated internally via fold expressions over member pointer types.

**Rendering**:
- Pre-rotation matrix aligns model coordinate system with game world
- Frustum culling based on camera visible area
- Death shrink effect using pow() function on destroy time ratio
- Transformation matrix combines scaling, pre-rotation, yaw rotation, and translation
- Writes GltfLayout structures to mapped GPU storage buffer
- Submits draw commands for both main rendering and shadow passes

**Design Pattern**: Structure of Arrays layout with dynamic allocation provides cache-friendly iteration while supporting variable enemy counts. Phase separation ensures rendering state (positions, directions, destroyed times) is independent from logic state (velocities, health, AI flags, weapon state). The `engine::ReallocateIfCapacityChanged()` pattern simplifies Update() methods by handling both null data early returns and capacity reallocation in a single call.

## Common Patterns

### SOA Layout with Dynamic Allocation

Collections organize data by access pattern:
- **Interpolate structures**: Rendering state (positions, directions) updated during interpolation phase
- **PostRender structures**: Logic state (velocities, health, flags) updated during PostRender phase
- Single contiguous allocation per structure with 64-byte alignment for cache efficiency
- RAII cleanup via `common::AlignedUniquePtr<std::byte>`

### Memory Management Helpers

Core patterns simplify dynamic allocation:
- **`engine::ReallocateIfCapacityChanged()`**: Used in Update() methods, handles null data (returns false for early exit) and capacity changes (reallocates and returns true). Buffer size calculated via fold expressions over member pointer types.
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
