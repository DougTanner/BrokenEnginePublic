# `/Engine/Source/Frame/Pools/`

High-performance object management systems using fixed-size pools with O(1) allocation/deallocation. These pools form the backbone of the engine's entity management, providing deterministic memory layouts for frame-based state management.

## Architecture Overview

### Memory Layout
Pools use structure-of-arrays (SoA) design for cache efficiency:
- `alignas(64)` ensures cache line alignment
- Separate arrays for flags, info, and objects
- Hot data (positions, active flags) grouped together
- Cold data (configuration) separated

### Allocation Strategy
- O(1) allocation by scanning `pbUsed[]` array
- O(1) deallocation by clearing used flag
- `uiMaxIndex` optimization skips empty tail slots
- No memory fragmentation - slots reused
- Deterministic allocation order for replays

### Thread Safety Model
- `giMultithreading` atomic flag controls synchronization
- Per-pool mutex for allocation/deallocation
- Read access generally lock-free during updates
- Careful ordering prevents race conditions

## Base Templates

### ObjectPool.h
Core template providing pool functionality for all entity types.

**Template Parameters**:
- `T` - Info structure (configuration data)
- `U` - Object structure (runtime state)
- `V` - Index type (uint8_t or uint16_t)
- `POOL_SIZE` - Maximum objects (compile-time constant)

**Key Members**:
```cpp
alignas(64) bool pbUsed[POOL_SIZE + 1];      // Active slot flags
alignas(64) T pObjectInfos[POOL_SIZE + 1];   // Configuration data
alignas(64) U pObjects[POOL_SIZE + 1];       // Runtime state
V uiMaxIndex;                                 // Highest used index
```

**Core Methods**:
- `Add(const T& info)` - Allocates object, returns handle
- `Remove(V handle)` - Deallocates object by handle
- `Copy()` - Fast frame state duplication
- `operator[]` - Direct array access for updates

**Design Notes**:
- Arrays sized `POOL_SIZE + 1` to allow end iterators
- Trivially copyable requirement for all stored types
- Static assert prevents overflow of index type

### ObjectControllerPool.h
Extends ObjectPool for time-based keyframe animations.

**Additional Features**:
- Stores animation keyframes and timing
- Automatic interpolation between keyframes
- Self-destruction when animation completes
- Links to target objects in other pools

**Usage Pattern**:
```cpp
// Define keyframes
LightKeyframe frames[] = {{0.0f, brightColor}, {1.0f, dimColor}};

// Add controller
auto handle = lightControllers.Add(frames, targetLight);

// Controller auto-updates target each frame
// Auto-removes when animation ends
```

## Pool Type Categories

### 1. Visual Effects Pools

**Billboards** - 2D sprites in 3D space
- Camera-facing quads with texture
- Supports rotation, scaling, color tint
- Used for: Particles, UI elements, debug markers

**HexShields** - Hexagonal shield effects  
- Specialized shader for sci-fi shields
- Impact ripple animations
- Energy field distortion

**Splashes** - Water/impact effects
- Particle burst animations
- Velocity-based dispersion
- Gravity simulation

**Smoke** - Volumetric smoke system
- Puffs: Individual smoke particles
- Trails: Connected smoke paths
- Spreading/dissipation simulation

### 2. Lighting Pools

**AreaLights** - Polygon-based light sources
- 4-vertex quad lights
- Soft shadow support
- Color and intensity

**PointLights** - Omnidirectional lights
- Position-based attenuation
- Dynamic color/intensity
- Shadow casting optional

**PointLightControllers** - Animated lights
- Flicker, pulse, strobe effects
- Color transitions
- Synchronized patterns

### 3. Physics Pools

**Areas** - Damage/trigger zones
- Polygon collision detection
- Damage over time application
- Entry/exit callbacks

**Pullers** - Attraction forces
- Gravitational pull simulation
- Variable strength/radius
- Black hole effects

**Pushers** - Repulsion forces  
- Explosion shockwaves
- Wind effects
- Radial force fields

### 4. Gameplay Pools

**Targets** - Trackable positions
- Subscriber notification system
- Multiple subscriber support
- Auto-cleanup on destruction

**Sounds** - 3D audio sources
- Positional audio with velocity
- Unique ID tracking
- Doppler effect support

**Explosions** - Complex explosion effects
- Multiple visual components
- Damage application
- Force propagation

### 5. Navigation Pools

**Navmesh** - Pathfinding grid
- 16×16 navigation cells
- Distance field computation
- AI movement planning

## Implementation Details

### Pool Sizes and Types
Defined in project-specific `PoolConfig.h`:
```cpp
// Small pools (uint8_t index, max 254)
using area_t = uint8_t;
constexpr area_t kuiMaxAreas = 64;

// Large pools (uint16_t index, max 2046)  
using billboard_t = uint16_t;
constexpr billboard_t kuiMaxBillboards = 1024;
```

### Update Phases
Pools implementing `UpdateList` participate in:
1. **Global** - Single-threaded updates
2. **Collide** - Spatial queries (parallel)
3. **Spawn** - Process creation requests
4. **Destroy** - Cleanup destroyed objects
5. **RenderMain** - Submit draw calls

### Custom Pool Methods
Some pools override base methods:
- **Explosions**: `SetupExplosion()` initializes complex state
- **Sounds**: Custom `Add()` assigns unique IDs
- **Targets**: `Remove()` requires flags for validation
- **Splashes**: `PostRender()` for screen-space effects

## Performance Considerations

### Cache Optimization
- Hot/cold data separation
- Predictable memory access patterns
- Minimal pointer chasing
- SIMD-friendly layouts

### Parallel Processing
- Thread-local spawn buffers
- Lock-free reads during updates
- Atomic operations for allocation
- Work distribution by pool type

### Memory Usage
Example for 1024 billboards:
```
Flags:    1024 × 1 byte  = 1 KB
Info:     1024 × 64 bytes = 64 KB  
Objects:  1024 × 128 bytes = 128 KB
Total:    ~193 KB + padding
```

## Best Practices

### When to Use Pools
- High-frequency allocation/deallocation
- Need deterministic memory layout
- Fixed maximum count acceptable
- Performance critical systems

### Pool vs Collection
- **Pools**: Long-lived objects with handles
- **Collections**: Per-frame spawn requests

### Handle Safety
- Always check `IsValid()` before use
- Don't store handles across frame boundaries
- Use subscriber pattern for tracking

## Common Patterns

### Effect Chaining
```cpp
// Explosion creates multiple effects
explosion.Add(position);
for (int i = 0; i < debrisCount; ++i)
    billboards.Add(debrisSprite, position + random());
sounds.Add(explosionSound, position);
pushers.Add(position, shockwaveForce);
```

### Lifetime Management
```cpp
// Controller manages object lifetime
auto controller = smokeControllers.Add(fadeAnimation);
controller.SetTarget(smokeHandle);
// Smoke auto-removed when animation ends
```

### Spatial Queries
```cpp
// Find all objects in radius
for (auto i = 0; i <= areas.uiMaxIndex; ++i)
{
    if (!areas.pbUsed[i]) continue;
    if (Distance(areas.pObjects[i].position, point) < radius)
        // Process area
}
```

## See Also
- `/Engine/Source/Frame/CLAUDE.md` - Frame architecture overview
- `/Engine/Source/Frame/Collections/CLAUDE.md` - Spawn request system
- `/Projects/*/Source/Frame/Pools/PoolConfig.h` - Size configuration