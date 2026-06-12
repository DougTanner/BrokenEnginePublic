# Collections - Game-Specific Object Collections for Space Combat

## Overview

SOA collections managing dynamic game entities (projectiles, enemies, players), built on the engine's `Collection<T>` template with paired `{Name}Interpolate`/`{Name}PostRender` structs matching the `FrameInterpolate`/`FramePostRender` split. All collections compile in both client and server builds, with render methods and visual fields gated by `#ifdef BT_CLIENT`.

See also: [Engine collections hub](../../../../../Engine/Source/Frame/Collections/CLAUDE.md) for the Collection\<T\> framework, Sync/Controller patterns, CRC architecture, and shared conventions — not duplicated here.

## Key Classes/Systems

- **[Blasters](Blasters/CLAUDE.md)** - Fast-moving energy projectiles with terrain impact effects and swept sphere collision
- **[Missiles](Missiles/CLAUDE.md)** - Guided homing missiles with AI tracking, area damage, and smoke trails
- **[Targets](Targets/CLAUDE.md)** - Trackable world positions for missile guidance, referenced by stable ID with subscriber-based lifetime
- **[Spaceships](Spaceships/CLAUDE.md)** - AI-controlled enemies with health, weapons, skeletal animation, and terrain interaction
- **[Players](Players/CLAUDE.md)** - Player spaceships with stable ID lookup, AI behavior, weapons, shields, and cross-cell transfer

## Architecture Notes

Conventions below are hub-owned; child CLAUDE.mds document only deviations.

**Initialization**: Collections provide `Register()` for type registration at startup, and `GraphicsResources()` (client-only) for GPU buffer and pipeline creation — the interface is uniform, with empty stubs where a collection owns no GPU resources.

**Client Object Hydration**: Collections with client-only owned objects (area lights, wind trails, sounds) provide `ClientInit()` and `ClientInitAll()` to create visual/audio objects after receiving server state, since server streams exclude client-only fields. Players is the exception — its Update loop lazily re-adds missing wind trails and hex shields, so it has no hydration hook.

**Cross-Cell Transfer**: All collections use a two-phase approach: PostCollision flags out-of-bounds entities with `kTransfer`, then a dedicated Transfer phase generates `TransferRequest`s and destroys the entity. This separation ensures AreaDamage can skip transferring entities. Spawn and Transfer sites validate position/velocity XMVECTORs via `common::ValidateVector<IS_POSITION>()` to catch W-lane corruption at grid-boundary handoff.

**PreCollision Collision Layers**: Players, Blasters, Missiles, and Spaceships each register a collision layer in `PreCollision`, building per-entity flag/radius/damage arrays into heap-suppressed `thread_local` vectors (the per-Frame tick fans out via `Dispatch`). Those arrays' `.data()` must stay valid through `PostCollision`, so the workbuffer cannot back them.

**LogDifferences**: All collections provide `LogDifferences()` for desync diagnosis, logging each mismatched shared (non-client-only) field via `common::LogDifference`.

**File Splitting**: All five collections split across multiple `.cpp` files sharing one `.h`, with file names describing responsibility rather than lifecycle phase — Players and Spaceships split by domain (Navigation, Combat); the rest use Update. `{Name}.cpp` is the default bucket for registration, lifecycle, and phase orchestrators; `{Name}Render.cpp` (`#ifdef BT_CLIENT`) holds rendering.

**Arrival Grace Period**: Players and Spaceships arriving via StatusChange (transfer or spawn) get a 1-second `pfArrivalGracePeriods` timer. While > 0, other entities skip them in targeting and behavior scans — no chase/flee/fire, and missile homing (`kDestination`) is deferred until expiry. Collision and damage still apply normally — invulnerability would hide real physics interactions, and collisions are unlikely during the first second at the frame edge. The timer rides in `TransferData`; `SpawnTransfer` resets it to the full duration since each transfer is a new StatusChange clients must sync.

**Pending Tick Countdown**: Delayed StatusChange activation via `uint8_t` countdown fields is documented at "Delayed StatusChange activation" in [../CLAUDE.md](../CLAUDE.md).

**Flags Field Packing**: Multi-valued fields (nav direction, pending states) should be packed into the collection's `Flags` type using bit ranges and helper functions, rather than stored as separate SOA arrays. This reduces SOA field count and keeps related state together.

**Unconditional Store Rule**: In Update loops, every shared field must be loaded from `rPrevious` at the top and stored to `rCurrent` at the bottom, unconditionally — outside any early-exit branches (transfer lock, destroyed checks, etc.). Storing inside a conditional block leaves uninitialized memory in `rCurrent`, causing client/server desync. Exception: fields `memcpy`'d forward in `AllocateAndCopy` and mutated only at state transitions (see Missiles, Spaceships) need no per-tick re-store.

**Debug Render Data**: Debug primitives are computed at render time in a dedicated `DebugRender` phase (called per-coord after collection `EndRender`), not during Interpolate Update. Entity positions must be read from the fully-interpolated `FrameInterpolate`, never from PostRender — PostRender positions lag behind the rendered frame. Only flags, metadata, and static world positions (nav waypoints, island destinations) may come from PostRender.

## See Also

- Parent frame: [../CLAUDE.md](../CLAUDE.md) — tick pipeline phase order, two-tier collection dispatch (`FrameCollections.h` tuple helpers exclude Players), serialization/CRC protocol
