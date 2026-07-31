# Frame - Game Simulation

## Overview

Game frame state extends the engine base with space-combat collections and deterministic phase hooks. Each cell derives its island elevation and navigation inputs from packed static data.

Update `../../../../Documents/Architecture/FrameUpdatePipeline.md` when phase order or participation changes.

## Invariants

- `RunFrameTick` is shared by server simulation and client replay. It verifies the required floating-point environment before advancing deterministic state. CRC-affecting fixed-tick decisions use `Frame`, `FrameInput`, `FrameStaticData`, or explicit phase parameters, not `game::gpGame`, wall-clock time, or client render state; if required input is unavailable, stop and resolve ownership.
- Interpolate precedes PostRender update, collision, transfer, destroy, and spawn. Same-tick Interpolate state remains permitted PostRender input. Shared CRC is stamped after those phases and before server transfer harvest.
- Players use explicit dispatch for player-specific logic; other game collections use the tuples in `FrameCollections.h`. Register new collections in the correct tuple and preserve deterministic ordering.
- `SharedCrcMembers()` is a subset of `SharedMembers()`. Full `Write()`/`Read()` is build-local; `ServerRead()` consumes the shared cross-build format. Both paths require exact Interpolate/PostRender count and capacity parity across every registered collection pair; build-local reads replace the destination only after the full frame passes that check.
- `Frame::kiVersion` composes navigation and collection versions. Bump its base when CRC semantics change without a contributing version bump.
- Normalize a non-finite spaceship-spawn timer at save/replay and full-state read boundaries before the loop that repeatedly subtracts the spawn interval until the timer falls below it; finite values remain unchanged.
- Navigation and elevation are deterministic derived data excluded from both the CRC and persisted frame payload. Server-built navigation is sent to clients.
- Status changes are consumed here: `kUpdatePlayer` and `kUpdateFleet` in Update; spawn and destroy handling in Spawn. Their serialized tags and append-only wire rule are owned by game Network (`../Network/AGENTS.md`).
- Delayed behavior updates use countdowns that transfer with the entity; transfer, spawn, and destroy status changes apply immediately.
- `Frame::GetMissileTarget` is the shared enemy-target query behind missile and player homing. It prefers targets that the fewest other shots are already homing on (spreading volleys around) and registers a new subscriber when it succeeds, so despite the `Get` name it writes to the frame, and the order calls happen in within a tick is part of the deterministic stream. Do not hoist it out of a loop, cache its result, or run it in parallel across entities.
- The game phase overrides, not the engine base, clear the shared collision and area-damage queues, and they clear only after every collection has run that phase. A collection that consumes either queue must be registered so it runs ahead of that clear.
- Bounds helpers use the `vecArea` lane convention expected by `common::InsideArea`. Seed coord-local RNG streams with distinct multipliers.

## See Also

- `../../../../Engine/Source/Frame/AGENTS.md`
- Game collections: `Collections/AGENTS.md`
- `../../../../Documents/Architecture/GameReconciliation.md`
