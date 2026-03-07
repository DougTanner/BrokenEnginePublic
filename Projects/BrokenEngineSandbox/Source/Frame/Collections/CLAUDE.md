# /Projects/BrokenEngineSandbox/Source/Frame/Collections/

Game-specific object collections for space combat. Manages projectiles and enemies using phase-separated dynamic memory with Structure-of-Arrays layout. All game collections compile in both client and server builds, with render methods and visual fields `#ifdef BT_CLIENT`-gated.

## Architecture Overview

**Initialization Phases**: All Interpolate structs have two static initialization methods called during game startup:
- **`Register()`** - Type registration and configuration (e.g., area light types, explosion types, smoke trail types).
- **`GraphicsResources()`** - Client-only (`#ifdef BT_CLIENT`). Collections that render create their GPU buffers and pipelines; non-rendering collections have empty implementations.

## Core Collections

Each collection lives in its own subdirectory with a dedicated CLAUDE.md containing full architectural details.

| Collection | Purpose |
|------------|---------|
| **[Blasters](Blasters/CLAUDE.md)** | Fast-moving energy projectiles with terrain impact effects and swept sphere collision |
| **[Missiles](Missiles/CLAUDE.md)** | Guided homing missiles with AI tracking, area damage, and visual effects |
| **[Targets](Targets/CLAUDE.md)** | Trackable world positions for missile guidance with subscriber-based lifetime |
| **[Spaceships](Spaceships/CLAUDE.md)** | AI-controlled enemies with health, weapons, skeletal animation, and terrain interaction |
| **[Players](Players/CLAUDE.md)** | Player spaceships with stable ID lookup, AI behavior, weapons, shields, and cross-cell transfer |

## Common Patterns

### Memory Management

- `engine::Allocate()` for buffer reallocation, `engine::GrowPairedCollections()` for paired collection growth, `engine::DestroyElement()` for O(1) swap-with-last removal
- Collections early-out when `iCount == 0`

### Static vs Dynamic Fields

- **Static fields**: Set once in Spawn(), copied via `std::memcpy()` in AllocateAndCopy (e.g., alignment, acceleration)
- **Sync-written fields**: Written by owners via `Sync()` every frame, also memcpy'd so interpolates always have valid data
- **Dynamic fields**: Modified during Update() via load/save pattern (e.g., velocity, health, timers)

### Client Object Hydration (`#ifdef BT_CLIENT`)

Collections with client-only owned objects (area lights, wind trails, sounds, smoke trails) provide two static methods for managing those objects separately from server state:

- **`AllocateClientObjects(Frame&, int64_t)`** - Creates client-only owned objects for a single entity at a given index. Called during Spawn and transfer-based spawning. Extracted from the spawn path for reuse by `HydrateClientObjects`.
- **`HydrateClientObjects(Frame&)`** - Iterates all entities and calls `AllocateClientObjects` for each one. Called after receiving full state from the server (via `ApplyReceivedFullStates`), since server-format streams exclude client-only fields and the client must create visual/audio objects locally.

Blasters, Missiles, and Spaceships implement this pattern. Missiles additionally pass the smoke trail reuse ID for transfer continuity.

### Cross-Cell Transfer

All four collections (Blasters, Missiles, Spaceships, Players) use a two-phase approach for cross-cell entity migration. PostCollision flags out-of-bounds entities with `kTransfer`. The dedicated Transfer phase generates `TransferRequest`s with full gameplay state for seamless continuity, removes owned objects, and destroys the entity. This separation ensures AreaDamage can skip transferring entities and prevents premature removal before all collision phases complete. Smoke trail IDs are preserved across transfers for continuous rendering.

### Server Compare Pattern

All game collections (Blasters, Missiles, Spaceships, Targets) and Players provide `ServerCompare()` methods for desync diagnosis. Collections with client-only fields (Blasters Interpolate/PostRender, Missiles Interpolate/PostRender, Spaceships Interpolate/PostRender) implement explicit per-field `BreakOnNotEqual` on shared fields. Collections without client-only fields (Targets) delegate to `operator==`. The game-level Frame `ServerCompare()` dispatches to each collection's `ServerCompare()`.

### Extern Template Pattern

All collection headers declare `extern template struct Collection<T>` after the struct definitions, with explicit instantiations in the corresponding `.cpp` files. This follows the same pattern as engine collections to eliminate redundant `Collection<T>` instantiation across translation units.

### File Splitting Pattern

All collections live in their own `{Name}/` subdirectory. When a collection's `.cpp` exceeds size guidelines, split the implementation across multiple `.cpp` files sharing a single `.h`, organized by responsibility:

| File | Contents |
|------|----------|
| `{Name}.h` | All struct definitions, shared constants |
| `{Name}.cpp` | Lifecycle: registration, allocation, entity creation/transfer/destruction, equality comparisons |
| `{Name}Update.cpp` | Simulation: per-frame update logic, AI behavior, collision handling |
| `{Name}Render.cpp` | Rendering: GPU resources and draw submission (entire file `#ifdef BT_CLIENT`) |

Shared constants used across multiple `.cpp` files are declared in the `.h`; file-local constants stay in anonymous namespaces in their respective `.cpp` files.

Reference: `Collections/Players/` and `Collections/Spaceships/` implement the multi-`.cpp` splitting pattern.

### Adding New Members

Follow the 5-step pattern in the **add-collection-member** skill.

## See Also
- [Blasters/CLAUDE.md](Blasters/CLAUDE.md) - Energy projectile collection
- [Missiles/CLAUDE.md](Missiles/CLAUDE.md) - Guided homing missile collection
- [Targets/CLAUDE.md](Targets/CLAUDE.md) - Missile guidance target collection
- [Spaceships/CLAUDE.md](Spaceships/CLAUDE.md) - AI enemy spaceship collection
- [Players/CLAUDE.md](Players/CLAUDE.md) - Player spaceship collection
- Engine collections: [../../../../../Engine/Source/Frame/Collections/CLAUDE.md](../../../../../Engine/Source/Frame/Collections/CLAUDE.md)
- Parent frame: [../CLAUDE.md](../CLAUDE.md)
