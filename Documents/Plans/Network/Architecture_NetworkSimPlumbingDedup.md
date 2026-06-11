# Architecture: Network-Simulation Plumbing Dedup

## Context

Source: /external-architecture-review on `Engine/Source/Network` (recursive). The compile-time network-condition simulation (`keNetworkSimulation`) is plumbed through five near-identical sites across the client/server seam; a change to the sim model requires editing all five in lockstep.

## Design

### Extract the shared dispatch-with-delay block
- `Client::DispatchIncoming` + its delayed-queue block (`Client.cpp:116-163`) is near-identical to `Server::DispatchIncoming` + block (`Server.cpp:87-126`), including the same `miTimeMultiply > 1` fast-forward logic. Extract the sim-gated portion into a shared helper in `NetworkSimulation.h` (templated on the dispatch callable), leaving each side a thin call. [~45m]
- The delayed-packet purge recurs three more times with two distinct predicates: by slot channels on the client (`ClientSend.cpp:110-121` via `CoordSlotReliable`/`CoordSlotUnreliable`, `ClientReceive.cpp:536-543` via the equivalent `IsCoordChannel`+`ChannelToSlot` — same meaning, written differently) and by peer on the server (`Server.cpp:162-165`). Fold the two client slot purges into one named helper (e.g. `PurgeDelayedForSlot(deque, iSlot)`) beside the queue; either give the peer purge its own one-liner helper or a shared predicate-taking `PurgeDelayed` — grill picks. [~20m]

### Fold-in: sim RNG/counter ownership (from the refactor pass)
- `NetworkSimulation.h:87` `Random01` holds clock-seeded function-local static state; `ShouldDrop`'s counters (`:108`) and drop-count logs (`:139,153`) are more function-local statics. While extracting, move the LCG state + counters into a small struct owned next to `mDelayedPackets`, seeded from a constant — simulation runs become reproducible and resettable. [~20m]

## Critical files

- `Engine/Source/Network/NetworkSimulation.h`
- `Engine/Source/Network/Client/Client.cpp`, `ClientSend.cpp`, `ClientReceive.cpp`
- `Engine/Source/Network/Server/Server.cpp`

## Out of scope

- The real (non-sim) receive dispatch switchboards (`Client.cpp:179-220`, `Server.cpp:185-228`) — only the sim-gated wrapper is extracted; per-message handling stays put.
- Changing sim behavior (delay model, drop model, bounds) — pure dedup plus state-ownership move.
- The wire-format pairing work (`Architecture_WireFormatPairing.md`) — independent; the sim's magic byte-offset tick parse is owned there.

## Acceptance criteria

- One definition each for the dispatch-with-delay block and the purge logic; `keNetworkSimulation == kDisabled` builds remain zero-cost (`if constexpr` paths compile out identically).
- With sim enabled, behavior matches current (delays, drops, fast-forward flush), now reproducible via the constant seed.

## Notes

- **Invariant exposure**: none on the wire or CRC — the sim is compile-time-gated dev tooling, and the extraction leaves the real dispatch path mechanically identical. Main-thread-only assumptions unchanged (see `Architecture_SpecDocReconciliation.md` for documenting that contract).

## Verification Notes

Verified against source (2026-06-10); one bullet corrected:
- Similarity confirmed real and templatable: `Client::DispatchIncoming` (`Client.cpp:142-163`) and `Server::DispatchIncoming` (`Server.cpp:105-126`) are line-for-line identical apart from the `Receive` callable's signature (client takes no peer); the delayed-queue blocks in the two `Poll`s (`Client.cpp:116-131`, `Server.cpp:87-102`) are likewise identical including the `miTimeMultiply > 1` `FlushDelayed` vs `ProcessDelayed` split. A helper templated on the dispatch callable covers both.
- Zero-cost property survives: both call sites already gate on `if constexpr (keNetworkSimulation != kDisabled)`; the extracted helper is called only inside those gates, so `kDisabled` builds compile out identically.
- Corrected the purge item: the three remaining purge sites use two distinct predicates (slot-channel ×2 client-side, peer ×1 server-side) — a single `FlushDelayedFor(peer)` shape did not fit all three; bullet now folds the two equivalent client purges and leaves the peer purge as its own helper/predicate (grill).
- RNG/counter statics confirmed: clock-seeded `Random01` LCG state at `NetworkSimulation.h:87`, `ShouldDrop` per-channel counters at `:108`, drop-count log statics at `:139` and `:152` (plan's `:153` was one off — refreshed).
- Note for execution: the statics are header-inline shared across both sides' TUs, but only one of `Client`/`Server` exists per process, so moving them into per-owner structs next to `mDelayedPackets` changes no observable behavior.
