# `/Engine/Source/Frame/Collections/`

Template infrastructure for managing spawn requests - the mechanism for creating new game objects during frame updates.

## Architecture Overview

Collections solve a key problem in the frame-based architecture: how to create new objects in a deterministic way during parallel updates. Instead of directly creating objects (which would cause race conditions), game logic adds "spawn requests" to collections. These requests are then processed in a controlled manner during the Spawn phase.

## Core Files

### Collections.h

Template class providing spawn request buffering and management.

**Template**: `Spawnable<T, SIZE>`
- `T` - Spawn information structure (e.g., `SpawnBlaster`, `SpawnMissile`)
- `SIZE` - Maximum spawns per frame (compile-time constant)

**Key Members**:
- `T pSpawns[SIZE]` - Fixed-size array of spawn requests
- `int64_t iSpawnCount` - Current number of pending spawns

**Methods**:
- `AddSpawn(const T& rSpawn)` - Thread-safe spawn request addition
  - Returns true if added successfully
  - Returns false if buffer full (SIZE exceeded)
  - Atomically increments spawn count
- `static Interpolate()` - Frame state copying
  - Copies spawn requests from previous to current frame
  - Used when frame updates are skipped
- `operator==()` - Equality comparison for save state verification

## Design Principles

### Thread Safety
Collections are designed for concurrent access during parallel updates:
- `AddSpawn()` uses atomic operations when `giMultithreading > 0`
- Each thread can safely add spawns without locks
- Spawn processing happens single-threaded in Spawn phase

### Memory Efficiency
- Fixed-size arrays prevent dynamic allocations
- Stack allocation keeps data cache-friendly
- Trivially copyable for fast frame copies
- `alignas(64)` alignment prevents false sharing

### Determinism
Spawn order is preserved for reproducible gameplay:
- Spawns processed in order received
- No sorting or reordering of requests
- Frame number included for timing verification

## Usage Pattern

### 1. Define Spawn Structure
```cpp
struct SpawnBlaster
{
    XMFLOAT4 f4Position;
    XMFLOAT4 f4Velocity;
    float fDamage;
    int64_t iOwner;
};
```

### 2. Create Collection Class
```cpp
struct Blasters : public Spawnable<SpawnBlaster, 128>
{
    // Active blaster arrays
    XMFLOAT4A pf4Positions[kMaxBlasters];
    XMFLOAT4A pf4Velocities[kMaxBlasters];  
    float pfDamages[kMaxBlasters];
    
    // Update methods
    static void Global(Frame& rFrame, ...);
    static void Spawn(Frame& rFrame, ...);
    static void Collide(Frame& rFrame, ...);
};
```

### 3. Request Spawns
```cpp
// During update (can be from multiple threads)
SpawnBlaster spawn;
spawn.f4Position = position;
spawn.f4Velocity = velocity;
rFrame.blasters.AddSpawn(spawn);
```

### 4. Process Spawns
```cpp
void Blasters::Spawn(Frame& rFrame, ...)
{
    // Process all spawn requests
    for (int i = 0; i < rFrame.blasters.iSpawnCount; ++i)
    {
        const auto& spawn = rFrame.blasters.pSpawns[i];
        // Find free slot and initialize blaster
        // Copy spawn data to active arrays
    }
    // Reset for next frame
    rFrame.blasters.iSpawnCount = 0;
}
```

## Relationship to Pools

Collections and Pools work together:
- **Collections**: Temporary spawn requests + active object arrays
- **Pools**: Persistent object management with allocation/deallocation

Example flow:
1. Player fires weapon → `AddSpawn()` to Blasters collection
2. Blaster hits target → `AddSpawn()` to Explosions collection  
3. Explosion spawns → `Add()` to Explosions pool
4. Visual effect plays → `Remove()` from Explosions pool

## Common Patterns

### Chained Spawning
One spawn can trigger others:
```cpp
// In Missiles::Collide()
if (hit)
{
    rFrame.explosions.AddSpawn({position, size});
    rFrame.sounds.AddSpawn({position, "explosion.wav"});
}
```

### Spawn Validation
Validate before spawning:
```cpp
if (CanAffordUnit(unitType) && IsValidPosition(position))
{
    rFrame.units.AddSpawn({unitType, position});
}
```

### Batched Spawning
Multiple related spawns:
```cpp
for (int i = 0; i < particleCount; ++i)
{
    rFrame.particles.AddSpawn({position, RandomVelocity()});
}
```

## See Also
- `/Engine/Source/Frame/Pools/` - Object pool management
- `/Projects/*/Source/Frame/Collections/` - Game-specific implementations
- `/Engine/Source/Frame/UpdateList.h` - Update phase definitions