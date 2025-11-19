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

**Architecture**: Two independent structures with dynamic memory allocation:
- **BlastersInterpolate**: Position and area light data for rendering
- **BlastersPostRender**: Velocity and flag data for logic
- Each structure independently tracks count and capacity for proper serialization

**BlasterFlags**: Enum class defining blaster state (destroy flag) with typesafe flags wrapper.

**BlastersInterpolate**:
- Dynamically allocated position and area light arrays
- Static Update() integrates velocity into position using previous frame state and delta time
- Uses `engine::ReallocateIfCapacityChanged()` to handle capacity synchronization with previous frame
- Macro-based member list (BLASTERS_INTERPOLATE_LIST) for serialization helpers
- Full serialization support (equality, checksum, stream operators)

**BlastersPostRender**:
- Dynamically allocated flag and velocity arrays
- Static Update() copies flags and velocities from previous frame
- Static Collide() performs terrain collision detection and marks destroyed blasters
- Static Spawn() creates new blasters with automatic capacity growth using `engine::GrowCapacityWithCopy()`
- Static Destroy() removes flagged blasters using `engine::SwapElement()` for efficient unordered removal
- Macro-based member list (BLASTERS_POST_RENDER_LIST) for serialization helpers
- Full serialization support (equality, checksum, stream operators)

**Memory Management**:
- Single contiguous allocation via `common::AlignedUniquePtr<std::byte>` with 64-byte alignment
- Update methods use `engine::ReallocateIfCapacityChanged()` which handles both null data (returns false) and capacity changes (returns true)
- Spawn method uses `engine::GrowCapacityWithCopy()` for automatic capacity growth (2 * capacity + 1) with data preservation
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
- **SpaceshipsInterpolate**: Position, direction, and destroyed time data for rendering
- **SpaceshipsPostRender**: Velocity, health, AI state, and weapon data for logic
- Each structure independently tracks count and capacity for proper serialization

**SpaceshipFlags**: Enum class defining AI behavior flags (flee player, exploding, return to island center) with typesafe flags wrapper.

**SpaceshipsInterpolate Structure**:
- Dynamically allocated position, direction, and destroyed time arrays
- Static Update() integrates velocity into position and rotates direction using previous frame state and delta time
- Update() uses pattern: `if (!engine::ReallocateIfCapacityChanged(...)) { return; }` to handle null data and capacity changes
- Instance Render() submits GPU rendering commands with frustum culling and death shrink effects
- Macro-based member list (SPACESHIPS_INTERPOLATE_LIST) for serialization helpers
- Full serialization support (equality, checksum, stream operators)
- Serializes count, capacity, and only active elements to minimize file size

**SpaceshipsPostRender Structure**:
- Dynamically allocated arrays for flags, velocities, rotation, health, freeze times, explosion timers, and weapon state
- Static Update() processes AI logic, weapon firing, and physics using previous frame state and delta time
- Update() uses pattern: `if (!engine::ReallocateIfCapacityChanged(...)) { return; }` to handle null data and capacity changes
- Static Collide() handles spaceship collision detection
- Static Spawn() creates new spaceships with automatic capacity growth using `engine::GrowCapacityWithCopy()`
- Static Destroy() removes destroyed spaceships
- Macro-based member list (SPACESHIPS_POST_RENDER_LIST) for serialization helpers
- Full serialization support (equality, checksum, stream operators)
- Serializes count, capacity, and only active elements to minimize file size

**Memory Management**:
- Single contiguous allocation via `common::AlignedUniquePtr<std::byte>` with 64-byte alignment
- Update methods use `engine::ReallocateIfCapacityChanged()` which handles both null data (returns false) and capacity changes (returns true). Buffer size calculated internally via fold expressions over member pointer types.
- Spawn method uses `engine::GrowCapacityWithCopy()` for automatic capacity growth (2 * capacity + 1) with data preservation. Buffer size calculated internally via fold expressions over member pointer types.
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

Three core patterns simplify dynamic allocation:
- **`engine::ReallocateIfCapacityChanged()`**: Used in Update() methods, handles null data (returns false for early exit) and capacity changes (reallocates and returns true). Buffer size calculated via fold expressions over member pointer types.
- **`engine::GrowCapacityWithCopy()`**: Used in Spawn() methods, grows capacity (2 * capacity + 1) and preserves existing data. Buffer size calculated via fold expressions over member pointer types.
- **`engine::AllocateAndRead()`**: Used in deserialization, allocates buffers and sets up member pointers from stream. Buffer size calculated via fold expressions over member pointer types.

### Serialization Support

Each structure provides full serialization:
- Equality comparison for change detection
- Static Checksum() for replay validation
- Stream operators for hierarchical serialization
- Serializes count, capacity, and only active elements to minimize file size

### Macro-Based Member Lists

Structures use macros to define member lists for serialization helpers:
- Example: `#define BLASTERS_INTERPOLATE_LIST(a) a.pVecPositions`
- Example: `#define SPACESHIPS_POST_RENDER_LIST(a) a.pFlags, a.pVecVelocities, a.pfDeltaRotations, a.pfHealths, a.pfFreezeTimes, a.pfDestroyedExplosionTimes, a.pfNextBlasterSpawnTimes, a.piBlasterSpawns`
- Used by `engine::MultiCrc()` for checksum calculation
- Used by `engine::MultiWrite()` for serialization
- Used by `engine::AllocateAndRead()` for deserialization
- Fold expressions over member pointer types calculate buffer sizes internally
- Ensures consistency across CRC, serialization, and allocation
- Comment markers remind developers to update both macro and equality operators when adding members

## Integration with Engine

**Frame Integration**: Collections are members of FrameInterpolate and FramePostRender structures, participating in hierarchical version tracking and serialization.

**Update Flow**: Static Update() methods called from parent frame's update phases, receiving previous frame state and delta time for deterministic simulation.

## See Also
- Engine collections base: [../../../../../Engine/Source/Frame/Collections/CLAUDE.md](../../../../../Engine/Source/Frame/Collections/CLAUDE.md)
- Engine object pools: [../../../../../Engine/Source/Frame/Pools/CLAUDE.md](../../../../../Engine/Source/Frame/Pools/CLAUDE.md)
- Parent frame: [../CLAUDE.md](../CLAUDE.md)
