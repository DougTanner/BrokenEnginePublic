# /Projects/BrokenEngineSandbox/Source/

Sample game implementation.

## Core Files in `/Projects/BrokenEngineSandbox/Source/`

### Game.h/cpp
- Main game class inheriting from GameBase
- Coordinates subsystems: Frame, Input, UI, Graphics pipelines
- Key methods: Init(), CreateFrame(), UpdateFrame(), Render()

### Version.h
- Game version constants

### Pch.h
- Precompiled header

## Game Systems

### Frame/

#### Frame.h/cpp
- Core game state and simulation logic
- Manages all collections (spaceships, missiles, blasters)
- Collision detection and game rules
- Terrain/water interaction, island boundaries

#### Camera.h/cpp
- Third-person camera with smooth following
- View/projection matrix generation
- Zoom, rotation, position tracking
- Water reflection support

#### Player.h/cpp
- Player spaceship controller
- Movement physics
- Weapon systems (blasters, missiles, shields)
- Special abilities (dash, armor)

#### HealthDamage.h
- Damage types: Blaster, Missile, Collision, Shield
- Team identification

### Frame/Collections/

#### Blasters.h/cpp
- Projectile weapon system
- Terrain/object collision
- Visual effects (particles, lighting)

#### Missiles.h/cpp
- Homing missile system
- Target tracking and guidance
- Area damage and explosions

#### Spaceships.h/cpp
- AI-controlled enemies
- Pathfinding and combat behavior
- Multiple ship types

### Frame/Pools/

#### PoolConfig.h
- Object pool size configuration

### Graphics/

#### GltfPipelines.h/cpp
- Model rendering pipelines
- Spaceship, missile, terrain rendering
- Shader configuration

### Input/

#### Input.h/cpp
- Control mapping for keyboard/mouse/gamepad
- Menu and gameplay controls

### Ui/

#### Ui.h/cpp
- Menu system (main, pause, settings)
- HUD elements (health, radar, crosshair)

#### Wrapper.h
- Type-safe widget access

#### Localization.h
- UI text constants

### Profile/

#### GameProfile.h
- Performance timing markers

## See Also
- Engine: [../../../Engine/Source/CLAUDE.md](../../../Engine/Source/CLAUDE.md)