# Collections - Game-Specific Object Collections for Space Combat

## Overview

SOA collections managing dynamic game entities (projectiles, enemies, players) using phase-separated memory with the engine's `Collection<T>` template. All collections compile in both client and server builds, with render methods and visual fields gated by `#ifdef BT_CLIENT`. Each collection lives in its own subdirectory.

## Key Classes/Systems

- **[Blasters](Blasters/CLAUDE.md)** - Fast-moving energy projectiles with terrain impact effects and swept sphere collision
- **[Missiles](Missiles/CLAUDE.md)** - Guided homing missiles with AI tracking, area damage, and smoke trails
- **[Targets](Targets/CLAUDE.md)** - Trackable world positions for missile guidance with subscriber-based lifetime
- **[Spaceships](Spaceships/CLAUDE.md)** - AI-controlled enemies with health, weapons, skeletal animation, and terrain interaction
- **[Players](Players/CLAUDE.md)** - Player spaceships with stable ID lookup, AI behavior, weapons, shields, and cross-cell transfer

## Architecture Notes

**Initialization**: Collections provide `Register()` for type registration at startup, and `GraphicsResources()` (client-only) for GPU buffer and pipeline creation.

**Client Object Hydration**: Collections with client-only owned objects (area lights, wind trails, sounds) provide `ClientInit()` and `ClientInitAll()` to create visual/audio objects after receiving server state, since server streams exclude client-only fields.

**Cross-Cell Transfer**: All collections use a two-phase approach: PostCollision flags out-of-bounds entities with `kTransfer`, then a dedicated Transfer phase generates `TransferRequest`s and destroys the entity. This separation ensures AreaDamage can skip transferring entities.

**LogDifferences**: All collections provide `LogDifferences()` for desync diagnosis, logging each mismatched shared (non-client-only) field via `common::LogDifference`.

**File Splitting**: When a collection's `.cpp` exceeds size guidelines, it splits into `{Name}.cpp` (lifecycle), `{Name}Update.cpp` (simulation), and `{Name}Render.cpp` (rendering, `#ifdef BT_CLIENT`), sharing a single `.h`. See Players and Spaceships for examples.

**Extern Templates**: All collection headers declare `extern template struct Collection<T>` with explicit instantiations in the corresponding `.cpp` files.

## See Also

- Engine collections: [../../../../../Engine/Source/Frame/Collections/CLAUDE.md](../../../../../Engine/Source/Frame/Collections/CLAUDE.md)
- Parent frame: [../CLAUDE.md](../CLAUDE.md)
