# `/Engine/Source/Network/Client/` - Client Networking

## Overview

Client-side ENet networking split into the low-level `Client` class (connection, packet I/O, slot state machine) and the engine-generic `ClientSessionBase` (game-session orchestration). Client-only (`BT_CLIENT`).

## Key Classes

- **Client** - ENet peer managing server connection with coord slot subscription state machine (kUnsubscribed -> kSubscribing -> kWaitingFullState -> kActive -> kUnsubscribing), per-slot ACK tracking, cancelled subscription interception, pipeline RTT/jitter/bandwidth measurement, packet loss tracking, soft desync recovery, and debug frame support. Game-specific packets stored as raw bytes for game-layer parsing. Split across three `.cpp` files: core, receive, send
- **ClientSessionBase** - Engine-generic base for game client sessions. Owns `Client` and `NetworkDiscoveryScanner`. Provides connection lifecycle, LAN discovery with auto-restart on timeout, coord subscription queue management (build queue, unsubscribe stale, drain in parallel), update buffering into `CoordFrames`, extrapolation snapshot ring buffer management, clock correction with disconnect threshold, and confirmed-tick queries. Game layer inherits and adds reconciliation, full-state application, and desync handling

## Architecture Notes

- Subscription queue drains in parallel: `TrySubscribeNext` loops calling `SendSubscribe` (which claims a free slot and returns true, or returns false if none available) until the queue is empty or slots are exhausted. If a kSubscribing slot is cancelled before the server responds, the coord is tracked in `mCancelledSubscriptions` so that `ServerSubscribeAccept` and `ServerCoordFullState` can intercept and reject them
- Clock correction computes tick error from RTT and nudges the client timestep by up to 4 steps per frame, with a variable divisor: 1/16th of a tick when |error| >= 4 (aggressive catchup) or 1/64th otherwise (gentle). Target-behind uses hysteresis (threshold of 2 ticks) to prevent jitter on variable-latency connections. Disconnect is triggered only after `kiClockErrorDisconnectConsecutiveFrames` (4) consecutive frames at or above the disconnect threshold, preventing false disconnects from single-frame spikes
- Extrapolation snapshots use a ring buffer sized to `kiNetworkBufferSize` (128 entries, decoupled from tick rate), managed with head/count/offset indices

## See Also

- Parent: [../CLAUDE.md](../CLAUDE.md)
- Game-layer session: [../../../../Projects/BrokenEngineSandbox/Source/Network/CLAUDE.md](../../../../Projects/BrokenEngineSandbox/Source/Network/CLAUDE.md)
