# Network Server - Game Session and Managers

Server-only game networking. `ServerSession` is the game-policy wrapper over `engine::ServerSessionRuntime`, coordinating client, fleet, transfer, and broadcast managers. The runtime owns host/discovery/pacing and fixed poll/tick/paused phase order; game hooks mutate game state around the deterministic Frame tick.

## State Ownership

- `ServerSession` owns per-client player records containing global ID and coordinate. Registry mutations mirror `authorizedCoords` while the connection exists; game code uses the records, not `authorizedCoords`, for owned-player lookup.
- `authorizedCoords` remains the engine authority for subscription adjacency and server display.
- Fleets are keyed by persistent `ClientGuid` so they survive disconnect and reconnect: disconnect only clears that GUID's client id, it never erases the fleet, and the fleet's ships keep running on the server. A separate reap pass therefore handles deaths in fleets whose owner is gone — marking members dead and shifting the flagship — because the connected-client death path skips those fleets by design.
- Fleet RNG is seeded once, serialized with fleet state, and consumes exactly two 64-bit draws when generating a fleet identifier.
- A fleet's position within its client's list shifts on delete, so requests and queued server work that outlive a poll carry the generated fleet identifier and re-resolve it at consumption.
- The entities being re-attached are sorted by global ID before rebuilding client ownership so reconnect and save-load preserve creation order.
- Normal update prepares tick inputs; replay supplies its recorded `FrameInput`.

## Timing and Paused Availability

- Fixed-tick pacing converts the simulation interval to wall time, sleeps the bulk remainder with a high-resolution waitable timer, then spins through the final precision margin. Preserve tick-remainder accounting and the existing overshoot diagnostics when changing this path.
- Poll transport and LAN discovery before simulation work on every update, including while paused. A paused or other zero-tick update builds navigation data needed by pending subscriptions, services resync and new-subscription full-state queues, and flushes them so a newly connected client can receive initial state without a simulation tick.
- The runtime calls the after-poll hook at every poll of an update, and an update polls more than once, so each handler must empty the request queue it processes; a request left queued is applied a second time later in the same update. The before-poll hook clears only queues whose requests are meant to expire unprocessed.

## Deterministic Tick Contracts

- The active set combines subscriptions and player coordinates, always including origin; replay uses coordinates with live readers.
- Spawn assignment diffs origin player IDs across the tick and pairs new IDs with waiting clients in request order. The client GUID written into Frame state is the persistent relink key.
- Fleet navigation defers flagship updates through `StatusChange`s. Within `BuildFrameInputs`, waiting-client spawn construction, queued player updates, fleet timers and pending flagship updates, plus broadcast and pre-spawn snapshot capture run only on advancing updates; this work remains deferred through paused and other zero-tick updates.
- Agent-injected `StatusChange`s remain queued until a normal, advancing, frame-ready tick can consume them. Consumed changes use the same deterministic type grouping as broadcast serialization.
- Transfer destinations must be adjacent. Non-player transfers require a live destination; player transfer makes the destination live itself. Recompute the destination Frame CRC after applying arrived transfers: the frame tick computes each frame's `sharedCrc` before transfers land, so the harvest step re-runs `Frame::Crcs()` on every destination frame and logs the before and after values. Clients check their own simulated frame against the CRC the server broadcast for that tick, so skipping this recompute desyncs every client on the receiving end of a transfer. It is not defensive recomputation and must not be optimized away as redundant.
- `ServerSessionRuntime::CompleteTick` invokes `ServerBroadcaster::BuildTickPublication`, which passes each tick to `ServerSessionRuntime::PublishTick` to maintain the delta resend ring. The publication includes complete per-coordinate Frames for the separate diagnostic ring only when `kbDesyncDebugFrames` is enabled. This manual flag is disabled by default and must match the client build.

## Trust and Lifecycle Boundaries

- Engine and game packet contract gates run before manager dispatch. Side-specific bounds validate navigation timing, fleet sizes, and save/replay fleet data without partially applying a request.
- Connect-time new-client processing, including its ownership relink, begins only after the engine accepts `ClientHello` and marks the connection handshake complete.
- A client is dead only after all owned players are gone; skip death handling while a player is mid-transfer.
- Load reset clears client, transfer, and broadcast transient state after restored fleet state is read. Do not erase restored fleet RNG or pending navigation updates.
- When the last client leaves, clear network-driven pause and timescale on the nonempty-to-empty transition. An agent-paused server that was already empty retains its commanded state.

## See Also

- Engine Server Transport: `../../../../../Engine/Source/Network/Server/AGENTS.md`
- `../../../../../Documents/Architecture/Network.md`
