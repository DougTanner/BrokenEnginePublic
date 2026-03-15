# `/Engine/Source/Network/Client/` - Client Networking

## Overview

Client-side ENet networking split into the low-level `Client` class (connection, packet I/O, slot state machine) and the engine-generic `ClientSessionBase` (game-session orchestration). Client-only (`BT_CLIENT`).

## Key Classes

- **Client** - ENet peer managing server connection, coord slot subscriptions with state machine (kUnsubscribed -> kSubscribing -> kWaitingFullState -> kActive -> kUnsubscribing), per-slot ACK tracking, cancelled subscription interception, pipeline RTT measurement, and `SendResyncRequest()` for soft desync recovery (requests full state re-download from server). Game-specific packets are stored as raw bytes for game-layer parsing. Split across three `.cpp` files: core (`Client.cpp`), receive (`ClientReceive.cpp`), send (`ClientSend.cpp`)
- **ClientSessionBase** - Base class for game-level client sessions. Owns `Client` and `NetworkDiscoveryScanner`. Provides connection lifecycle, coord subscription mechanics (queue-based one-at-a-time subscribe, stale unsubscribe), update buffering into `CoordFrames`, extrapolation snapshot ring buffer management, clock correction with disconnect threshold, and confirmed-tick queries (`GetConfirmedTick()` for global minimum across all subscribed coords, `GetHumanConfirmedTick()` for just the human player's coord). Game layer inherits and adds reconciliation, full-state application, and desync handling

## Architecture Notes

- Subscription queue processes one coord at a time, waiting for in-flight operations to complete before subscribing the next. If a kSubscribing slot is cancelled before the server responds, the coord is tracked in `mCancelledSubscriptions` so that `ServerSubscribeAccept` and `ServerCoordFullState` can intercept and reject them
- Clock correction computes tick error from RTT and nudges the client timestep by up to 4/64ths of a tick per frame. Target-behind uses hysteresis (threshold of 2 ticks) to prevent jitter on variable-latency connections
- Extrapolation snapshots use a ring buffer sized to one second of ticks, managed with head/count/offset indices

## See Also

- Parent: [../CLAUDE.md](../CLAUDE.md)
- Game-layer session: [../../../../Projects/BrokenEngineSandbox/Source/Network/CLAUDE.md](../../../../Projects/BrokenEngineSandbox/Source/Network/CLAUDE.md)
