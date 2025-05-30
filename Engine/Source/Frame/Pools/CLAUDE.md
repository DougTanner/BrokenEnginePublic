# /Engine/Source/Frame/Pools/

The `/Engine/Source/Frame/Pools/` directory contains efficient object managers for game entities using fixed-size pools with O(1) allocation/deallocation. All pools are trivially copyable for frame-based state management and instantiated as members of FrameBase.

## Base Templates

### /Engine/Source/Frame/Pools/ObjectPool.h
Generic template for all object pools.
- Template: `ObjectPool<T, U, V, POOL_SIZE>` where T=Info, U=Object, V=Index type
- Tracks used slots with `pbUsed[]` array and maintains `uiMaxIndex` for iteration
- Thread-safe operations via mutex when `giMultithreading` is active
- Global singleton: `inline std::atomic<int64_t> giMultithreading = 0;`

### /Engine/Source/Frame/Pools/ObjectControllerPool.h
Extends ObjectPool for time-based interpolated animations.
- Manages keyframe arrays with automatic lerping between states
- Auto-destruction when animation time expires
- Links controllers to objects in associated pool
- Requires Info type to implement `static Lerp()` method

## Pool Implementations

### /Engine/Source/Frame/Pools/Areas.h/.cpp
Damage zones with polygon vertices.
- Class: `Areas : public ObjectPool<AreaInfo, Area, area_t, kuiMaxAreas>`
- Stores area polygon vertices and damage per second
- Type: `area_t` (uint8_t), max 254 areas

### /Engine/Source/Frame/Pools/Billboards.h/.cpp
Sprite rendering in 3D space.
- Class: `Billboards : public UpdateList, public ObjectPool<BillboardInfo, Billboard, billboard_t, kuiMaxBillboards>`
- Supports offscreen-only rendering, rotation, alpha transparency
- Static `RenderMain()` method for rendering pipeline
- Type: `billboard_t` (uint16_t), max 2046 billboards

### /Engine/Source/Frame/Pools/Explosions.h/.cpp
Particle explosions with effects.
- Class: `Explosions : public ObjectPool<ExplosionInfo, Explosion, explosion_t, kuiMaxExplosions>`
- Manages up to 8 trails per explosion
- Links to pusher system for force effects
- Custom `Add()` calls `SetupExplosion()` for initialization
- Type: `explosion_t` (uint8_t), max 254 explosions

### /Engine/Source/Frame/Pools/HexShields.h/.cpp
Hexagonal shield visual effects.
- Class: `HexShields : public UpdateList, public ObjectPool<HexShieldInfo, HexShield, hex_shield_t, kuiMaxHexShields>`
- Uses specialized shader layout (`shaders::HexShieldLayout`)
- Static `RenderMain()` for rendering pipeline
- Type: `hex_shield_t` (uint16_t), max 2046 shields

### /Engine/Source/Frame/Pools/Lighting.h/.cpp
Area and point light systems.
- Classes:
  - `AreaLights` - Quad-based lights with 4 vertices
  - `PointLights` - Position-based lights with color/intensity
  - `PointLightControllers` - Animated point lights
- Types: `area_light_t` and `point_light_t` (uint16_t), max 2046 each

### /Engine/Source/Frame/Pools/Pullers.h/.cpp
Attraction force system.
- Class: `Pullers : public ObjectPool<PullerInfo, Puller, puller_t, kuiMaxPullers>`
- Creates 2D attraction forces with radius and intensity
- `SetupZones()` configures force zones
- `ApplyPull()` calculates forces on positions
- Type: `puller_t` (uint8_t), max 254 pullers

### /Engine/Source/Frame/Pools/Pushers.h/.cpp
Radial force emitters.
- Class: `Pushers : public ObjectPool<PusherInfo, Pusher, pusher_t, kuiMaxPushers>`
- Creates outward pushing forces for physics effects
- Type: `pusher_t` (uint8_t), max 254 pushers

### /Engine/Source/Frame/Pools/Smoke.h/.cpp
Volumetric smoke effects.
- Classes:
  - `Puffs` - Individual smoke particles
  - `PuffControllers` - Animated smoke puffs
  - `Trails` - Smoke trail system with position history
- Global singletons:
  - `inline bool gbSmokeClear = true;`
  - `inline float gbSmokeSpread = false;`
- Static arrays in `Trails` for position tracking
- Types: `puff_t` and `trail_t` (uint8_t), max 254 each

### /Engine/Source/Frame/Pools/Sounds.h/.cpp
3D spatial audio management.
- Class: `Sounds : public ObjectPool<SoundInfo, Sound, sound_t, kuiMaxSounds>`
- Tracks unique sound IDs, position, velocity, volume, pitch
- Custom `Add()` assigns unique IDs to new sounds
- Custom `Copy()` preserves sound IDs during frame copies
- Type: `sound_t` (uint8_t), max 254 sounds

### /Engine/Source/Frame/Pools/Splashes.h/.cpp
Water/impact splash effects.
- Class: `Splashes : public ObjectPool<SplashInfo, Splash, splash_t, kuiMaxSplashes>`
- Manages particle-based splash animations
- `SetupSplash()` initializes effect parameters
- Static `PostRender()` for post-render updates
- Type: `splash_t` (uint16_t), max 2046 splashes

### /Engine/Source/Frame/Pools/Targets.h/.cpp
Targetable positions with subscribers.
- Class: `Targets : public ObjectPool<TargetInfo, Target, target_t, kuiMaxTargets>`
- Implements subscriber pattern for target tracking
- Flags: destination, subscriber, player, enemy
- Custom `Remove()` requires flags parameter (default deleted)
- Static `Interpolate()` for frame interpolation
- Type: `target_t` (uint8_t), max 254 targets

## Key Design Principles

- All pools use `alignas(64)` for cache line optimization
- Trivially copyable requirement enables fast frame state copies
- Fixed-size arrays prevent runtime allocations
- UpdateList inheritance provides multithreaded update/render methods
- Pool sizes/types defined in project-specific PoolConfig.h
