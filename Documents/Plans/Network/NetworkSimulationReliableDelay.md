# Delay Reliable Packets in NetworkSimulation (Per-Channel FIFO)

## Context

`NetworkSimulation` never delays reliable packets. In `EnqueueOrDrop` (`Engine/Source/Network/NetworkSimulation.h:137`), only unreliable-channel packets go through `ShouldDrop` + the delay queue (`:140-176`); the `else` branch (`:177-181`) hands reliable packets straight to `handleReliable` with **zero latency**. `DispatchOrEnqueue` (`:186`) routes all incoming packets through this. So handshake, subscribe/subscribe-accept, full-state, static-data, and unsubscribe flows — every reliable control message — are exercised at zero simulated latency even under the harshest preset (`kChina`, 300-500 ms). This is exactly where the audit's subscription-race bugs live (ghost adoption, unsubscribe-ack overtaking full states, out-of-order full-state vs subscribe-accept across channels), and why they survived network-simulation testing: the reliable ordering/timing that triggers them is never perturbed.

This is a prevention-investment change: it does not fix a shipping bug directly, it makes the compile-time network-simulation harness actually cover the reliable control plane where the desync races occur.

## Design

- Delay reliable packets through the same delay queue, **delay-only, never dropped**. The simulation sits above ENet; dropping a "reliable" packet here would be a permanent loss ENet cannot retransmit. Apply latency only (reuse `RandomOneWayDelay`, `:101`); skip `ShouldDrop` for reliable channels.
- **Preserve per-channel FIFO ordering** — ENet guarantees in-order delivery per channel, and the reliable control flows depend on it. The existing queue is sorted by absolute `releaseTime` (`:172-173`), so independent random delays could reorder two packets on the same channel. Enforce per-channel monotonic release: a new reliable packet's `releaseTime` is `max(now + delay, lastReleaseTimeOnThatChannel)`. Track last-release per channel in `NetworkSimulationState` (`:83`, sized `NetworkManager::kuiChannelCount`, reset with the RNG state).
- Keep the `if constexpr (keNetworkSimulation != kDisabled)` zero-overhead-when-disabled property (call sites `Server.cpp:108-114`, and the client equivalent) and the constant-seeded RNG reproducibility (`kuiSeed`, `:85`).
- **Do not extend the drop-log's fixed byte-offset tick parse** (`:150-153`, `memcpy` from `data + 4`) to reliable control packets — that offset mirrors the coord-packet header layout only and would misparse control packets. Reliable packets are delayed, not dropped, so they take no drop-log path; keep it that way.

## Critical files

- `Engine/Source/Network/NetworkSimulation.h` — `EnqueueOrDrop` (add reliable delay branch), `NetworkSimulationState` (per-channel last-release tracking), the insert/sort logic. `DispatchOrEnqueue` / `ProcessOrFlush` / `PurgeDelayedForSlot` unchanged in shape but now also carry reliable packets.
- Call sites unchanged in signature: `Server::DispatchIncoming` (`Engine/Source/Network/Server/Server.cpp:106`) and the client peer's equivalent dispatch (`Engine/Source/Network/Client/Client.cpp`).
- `Engine/Source/Network/CLAUDE.md` — update the `NetworkSimulation` entry: it currently documents "Reliable packets are passed through immediately"; the constant-seed reproducibility note stays.

## Acceptance criteria

- Under a non-disabled preset, reliable control packets arrive with the configured one-way latency, in per-channel FIFO order, and are never dropped.
- Disabled build path is byte-for-byte unchanged (compile-time gated; no runtime cost).
- Simulated runs remain reproducible (constant-seeded RNG; per-channel release tracking resets with peer reconstruction).

## Out of scope

- Applying loss to reliable packets (explicitly excluded — permanent loss ENet can't recover).
- Fixing the subscription-race bugs themselves (ghost adoption, unsubscribe/full-state ordering) — this plan only exposes them to testing; fixes are separate.
- The coord-packet drop-log offset parse and the `Architecture_WireFormatPairing.md` "retire the magic offset" work (that plan owns `NetworkSimulation.h:150-153`).
- Production behavior — the simulation is compile-time-disabled in shipping builds.

## Notes

- **Invariant exposure**: none in production — the entire path is behind `if constexpr (keNetworkSimulation != kDisabled)` and disabled in shipping builds; only simulated-network testing behavior changes. No wire/CRC/`kiVersion`/determinism exposure. The delay queue is the sole (suppressed) heap user; reliable packets now also copy into it.
- Per-region `NetworkSimulationBounds` tolerances (`:60-72`, the CRC/replay-depth maxima the game profiler's Network screen validates against) may need re-validation once reliables incur latency — reconciliation depth under, e.g., `kChina` could shift. Flag for measurement after landing.
- **Grill decision**: per-channel release-time tracking is the recommended FIFO mechanism; confirm whether a per-channel last-release timestamp in `NetworkSimulationState` is preferred over a per-channel sub-queue. No other open decisions.
