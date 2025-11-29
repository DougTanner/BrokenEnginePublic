# /Projects/BrokenEngineSandbox/Source/Frame/Collections/

Game-specific object collections for space combat. Manages projectiles and enemies using phase-separated dynamic memory with Structure-of-Arrays layout.

## Architecture Overview

**Phase Separation**: Collections split into Interpolate (rendering state) and PostRender (logic state) structures for deterministic replay.

**Memory Layout**: SOA with dynamic allocation - contiguous buffers with 64-byte alignment for cache-friendly iteration and SIMD optimization.

**Lifecycle**: Objects spawn via requests, update through frame phases, and destroy when flagged. Dynamic capacity growth handles variable counts.

## Core Collections

### Blasters.h/cpp

Fast-moving energy projectiles. Uses shared BlasterType configuration for memory efficiency - player registers types at initialization, spawn uses type index to look up configuration. Integrates with Collision using kDestroyOnCollide flag to prevent multi-hit. Syncs positions to velocity-aligned area light quads for rendering. Spawns ControlledPointLight effects on terrain impact with 3-keyframe animation (flash → glow → fade out).

### Missiles.h/cpp

Placeholder for future guided missile system with homing behavior.

### Spaceships.h/cpp

AI-controlled enemies with health, weapons, and behavior flags. Inherits from both `engine::Collection` and `engine::Renderable` mixin for GPU pipeline support with automatic buffer resizing. Pre-tags exploding spaceships with kAlreadyCollided so they don't absorb blaster hits. Renders with frustum culling and death shrink effects.

## Common Patterns

### Memory Management

- `engine::ReallocateAndCopyMetadata()` - Buffer reallocation in Update phase
- `engine::GrowPairedCollections()` - Capacity growth for paired Interpolate/PostRender collections
- `engine::SwapElement()` - O(1) unordered removal

### Serialization

Collections provide `Members()` returning `std::tie()` of SOA member pointers. Engine template functions use this for CRC generation, stream I/O, and buffer size calculation via fold expressions.

### Adding New Members

Follow the 5-step pattern in the **add-collection-member** skill: add to struct and Members(), equality comparison, load in Update(), save in Update(), initialize in Spawn().

## See Also
- Engine collections: [../../../../../Engine/Source/Frame/Collections/CLAUDE.md](../../../../../Engine/Source/Frame/Collections/CLAUDE.md)
- Parent frame: [../CLAUDE.md](../CLAUDE.md)
