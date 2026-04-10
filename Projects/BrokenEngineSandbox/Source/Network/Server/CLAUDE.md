# Network/Server/ - Server Session and Managers

## Overview

Server-side networking: thin tick-pipeline orchestration via `ServerSession` plus four focused manager classes for fleet lifecycle, cross-cell transfers, frame broadcasting, and client connect/disconnect. All classes are `#ifdef BT_SERVER` only.

## Key Classes

- **ServerSession** - Inherits `engine::ServerSessionBase`. Drives the server tick pipeline: active set computation, tick broadcasting, resync handling, subscription updates, and game packet parsing. Owns the four managers via `unique_ptr`; managers access each other through `gpServerSession`. Exposes `mClientOwnedPlayerIds` as shared state for subscription validation
- **ServerFleetManager** - Fleet lifecycle: create/spawn/respawn/death, flagship shift on death or transfer, fleet sync packets to clients, and save/load serialization. Also owns fleet-level navigation via `TickFleetTimers()`, which runs each tick to advance `Fleet::fFrameChangeTimer`, pick a new `wantedCoord` when the timer expires, and broadcast `kUpdateFleet` StatusChange events to all fleet members with `uiPendingFleetWantedCoordTicks` initialized to `kiTickRate` (countdown activation — see Frame/CLAUDE.md). `LookupFleetWantedCoord()` returns a `FleetLookupResult` carrying `bIsFlagship`, `fleetWantedCoord`, and `uiPendingFleetWantedCoordTicks` for spawn initialization. Navigation delay is per-fleet and configurable via `UpdateFleetNavigationDelay()`. Fleets are keyed by persistent `ClientGuid` in a single map — game state operations work identically whether the owning client is connected or not. Client connection status is only consulted for network sends
- **ServerTransferManager** - Cross-cell transfer harvesting each tick: collects entities crossing coord boundaries, spawns into destination coords, and queues subscription updates for affected clients
- **ServerBroadcaster** - Per-tick frame input building, status change broadcasting to subscribed clients, and update-player-request processing. Behavior-changing StatusChanges (`kUpdatePlayer`) initialize `puiPendingWeaponModeTicks` to `kiTickRate` (countdown activation — see Frame/CLAUDE.md)
- **ServerClientManager** - Client connect/disconnect lifecycle, spawn request queuing and fulfillment, player death detection, and pre-spawn snapshot management. Players persist on disconnect; reconnecting clients are re-linked by GUID

## Architecture Notes

- `ServerSession` is a thin orchestrator; domain concerns are fully delegated to the four managers
- Manager interdependencies are resolved through `gpServerSession` rather than direct cross-manager references
- `SubscriptionUpdate` (in `ServerSession.h`) is the shared struct used to queue coord updates between managers

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Network root (shared packet types and serialization)
- Engine base: `Engine/Source/Network/Server/ServerSessionBase.h`
- [Network Architecture](../../../../../Documents/Architecture/Network.md)
