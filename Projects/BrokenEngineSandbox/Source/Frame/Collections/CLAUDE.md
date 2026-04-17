# Collections - Game-Specific Object Collections for Space Combat

## Overview

SOA collections managing dynamic game entities (projectiles, enemies, players) using phase-separated memory with the engine's `Collection<T>` template. All collections compile in both client and server builds, with render methods and visual fields gated by `#ifdef BT_CLIENT`. Each collection lives in its own subdirectory.

See also: [Engine collections hub](../../../../../Engine/Source/Frame/Collections/CLAUDE.md) for the Collection\<T\> framework, Sync/Controller patterns, CRC architecture, and shared conventions — not duplicated here.

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

**File Splitting**: When a collection's `.cpp` exceeds size guidelines, split by **subsystem/domain** across multiple `.cpp` files sharing a single `.h`, with file names describing responsibility rather than lifecycle phase. `{Name}.cpp` is the default bucket for registration, lifecycle, and phase orchestrators, and `{Name}Render.cpp` (`#ifdef BT_CLIENT`) holds rendering. See Players (split into `Players.cpp`/`PlayersNavigation.cpp`/`PlayersCombat.cpp`/`PlayersRender.cpp`) and Spaceships for examples.

**Arrival Grace Period**: Players and Spaceships arriving via StatusChange (transfer or spawn) get a 1-second `pfArrivalGracePeriods` timer. While > 0, other entities skip them in targeting and behavior scans — Spaceships won't chase/flee/fire at grace-period Players, Players won't target grace-period Spaceships, and missiles won't home on them (kDestination deferred until expiry). Collision and damage still apply normally — invulnerability would hide real physics interactions, and collisions are unlikely during the first second at the frame edge. The timer is carried in TransferData; SpawnTransfer resets it to the full duration since each transfer is a new StatusChange clients must sync.

**Pending Tick Countdown**: Behavior-changing StatusChanges use `uint8_t` countdown fields (set to `engine::kiTickRate` by the server, decremented each frame tick, behavior takes effect at 0). Preferred over absolute tick comparison — simpler, smaller storage, no dependency on the global tick counter. Does NOT apply to entity transfers, spawns, or destroys.

**Flags Field Packing**: Multi-valued fields (nav direction, pending states) should be packed into the collection's `Flags` type using bit ranges and helper functions, rather than stored as separate SOA arrays. This reduces SOA field count and keeps related state together.

**Unconditional Store Rule**: In Update loops, every shared field must be loaded from `rPrevious` at the top and stored to `rCurrent` at the bottom, unconditionally — outside any early-exit branches (transfer lock, destroyed checks, etc.). Storing inside a conditional block leaves uninitialized memory in `rCurrent`, causing client/server desync.

**Navigation**: Entities moving to a destination must use `NavQueryDirection` (per-cell `NavData` from `FrameStaticData`), never straight-line steering.

**Debug Render Data**: Debug primitives are computed at render time in a dedicated `DebugRender` phase (called per-coord after collection `EndRender`), not during Interpolate Update. Entity positions MUST be read from the fully-interpolated `FrameInterpolate`, never from PostRender — PostRender positions lag behind the rendered frame. Only flags, metadata, and static world positions (nav waypoints, island destinations) may come from PostRender.

**Extern Templates**: All collection headers declare `extern template struct Collection<T>` with explicit instantiations in the corresponding `.cpp` files.

## See Also

- Engine collections: [../../../../../Engine/Source/Frame/Collections/CLAUDE.md](../../../../../Engine/Source/Frame/Collections/CLAUDE.md)
- Parent frame: [../CLAUDE.md](../CLAUDE.md)
