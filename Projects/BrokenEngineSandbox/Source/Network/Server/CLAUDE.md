# Network/Server/ - Server Session and Managers

## Overview

Server-side game networking (`BT_SERVER`). `ServerSession` is a thin orchestrator over four domain managers (fleet, transfer, broadcaster, client) accessed via `gpServerSession`. Per-tick flow: inbound packet parse and request queuing, active-set recompute, frame tick with transfer harvesting, then broadcast of delta plus full-snapshot status changes.

## Invariants

- **Parallel-vector invariant**: `ServerSession::mClientOwnedPlayerIds[iClientId]` and `ClientConnection::authorizedCoords` are index-aligned; every spawn / transfer / death / relink must mutate both in lockstep.
- **Active set**: rebuilt each tick from client subscriptions plus any coord with players plus pending destroy coords; `kOriginCoord` always included.
- **Client-dead gating**: a client is only marked dead when all owned players are gone; clients mid-transfer are skipped to avoid false positives while a player crosses a coord boundary.
- **Fleet ownership by `ClientGuid`**: fleets are keyed by persistent GUID so they survive disconnect/reconnect; a separate reap pass handles AI-managed disconnected fleets.
- **Flagship timer**: only advances when the flagship is alive, at its wanted coord, and not navigating a cell edge; direction changes emit as `kUpdateFleet` status changes via the countdown-activation pattern (see [Frame/CLAUDE.md](../../Frame/CLAUDE.md)).
- **Reconnect / load relink**: matching `pClientGuids[i]` across frames are sorted by global ID to preserve creation order before rebuilding parallel vectors. Load additionally resets all managers and ring buffers but preserves pending flagship updates since fleet restoration enqueues into it.
- **Allocation suppression**: every public manager entry point wraps its body in `ScopedSuppressAllocationTracking`.

## Notes

- Game packet payloads have the type byte stripped before dispatch; size checks are post-strip. Out-of-band requests use the reliable channel.
- Disconnect clears `kPaused` and restores `mTimeStep` to 1/1 once no clients remain.
- Pending-request queues are cleared at the start of each tick and drained in fixed order; requests for vanished clients are silently dropped.

## See Also

- [../CLAUDE.md](../CLAUDE.md)
- [Engine/Source/Network/CLAUDE.md](../../../../../Engine/Source/Network/CLAUDE.md)
- [Network Architecture](../../../../../Documents/Architecture/Network.md)
