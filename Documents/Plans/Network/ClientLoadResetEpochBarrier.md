<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-03T21:48:51.733Z","dependsOn":[]} -->
# Prevent Client Load Reset From Admitting Stale Coord Packets

## Context

The active Tier-3 navigation change exposed a separate, pre-existing client transport residual while
checking runtime criterion F (authoritative replay determinism). The server-only replay check now passes;
the client-only run still reports confirmed CRC mismatches. The navigation Plan
(`Documents/Plans/Frame/NavInflateMarginUvRelative.md`) owns meter-domain contour geometry and does not own
client load/reset transport behavior.

The false required condition is that receiving `ServerLoadNotification` forms a client epoch boundary that
prevents every pre-load coordinate packet from entering the freshly reset slot/ring state. The current
reset clears some state, but not the delayed transport queue or static-data buffer, and the receive
classification admits same-coordinate static/full packets while a slot is `kSubscribing` before the new
server epoch is known.

Current source proves the gap:

- `ClientSessionRuntime::ResetForServerLoad` (`Engine/Source/Network/Client/ClientSessionRuntime.cpp:149-159`)
  resets the clock and subscription state, calls `Client::ResetAllSlots`, clears received full states and
  per-slot updates, but does not clear `Client::mDelayedPackets` or `mReceivedStaticData`.
- `Client::ResetAllSlots` (`Engine/Source/Network/Client/Client.cpp:98-105`) zeroes slots and cancelled
  subscriptions, so it leaves no client-side pre-load epoch floor.
- `Client::Poll` clears transient receive vectors at entry (`Engine/Source/Network/Client/Client.cpp:146-152`),
  dispatches ENet events, then processes the delayed queue (`:198-205`). `ClientSessionRuntime::PollAndDrain`
  drains the load flag and resets at `:206-210`, then applies static data, full states, and updates at
  `:211-214`. A packet released before the notification can therefore be left in `mReceivedStaticData` or
  `mReceivedFullStates` when the reset runs; a packet released after the reset can enter the new slot state.
- `Client::ClassifyFullState` (`Engine/Source/Network/Client/ClientReceive.cpp:69-112`) checks coordinate
  identity but applies its epoch guard only to `kWaitingFullState` (`:91-95`); `kSubscribing` returns commit.
  `ServerCoordFullState` commits that result into the received-full-state ring and activates the slot
  (`:185-237`).
- `ServerCoordStaticData` (`ClientReceive.cpp:261-291`) likewise has no epoch guard for `kSubscribing` and
  accepts `kUnsubscribed`; it pushes data into `mReceivedStaticData`, which the game layer hydrates directly
  (`Projects/BrokenEngineSandbox/Source/Network/Client/ClientSessionReceive.cpp:13-33`).
- `ServerSubscribeAccept` installs the server epoch only later (`ClientReceive.cpp:444-528`, especially
  `:498-521`), so a stale same-coordinate packet can seed state before the epoch is available. Coordinate
  updates already require `kWaitingFullState` or `kActive` plus epoch (`ClientReceive.cpp:114-128`), but their
  reset/queue boundary must remain covered by the same audit.
- The load notification is sent on control reliable channel 0 (`Engine/Source/Network/Server/ServerSend.cpp:263-279`),
  while static/full state uses `NetworkManager::CoordSlotReliable(0)` (channel 2;
  `Engine/Source/Network/Server/ServerSend.cpp:13-77`, `Engine/Source/Network/NetworkManager.h:15-25`).
  `NetworkSimulation::EnqueueOrDrop` maintains FIFO only per channel (`Engine/Source/Network/NetworkSimulation.h:135-187`),
  so the cross-channel release order needed for the race is reachable under `kChina` (300--500 ms and 2.5%
  loss, `NetworkSimulation.h:25-35`). Slot reuse is an epoch increment on the server
  (`Engine/Source/Network/Server/Server.h:99-106`, `ServerReceive.cpp:361-375`), which provides the existing
  post-load identity signal without adding a wire field.

The replay evidence is direct: `Temp/NavInflateFinal100/def-rerun/F-replay-determinism-evidence.json` records
server `End replay 2162, looping` but client `CONFIRMED DESYNC after full rollback/replay` lines with
`ReplayTicks: 2 NewConfirmed: -1`; the corrected server-only run at
`Temp/NavInflateFinal100/F-authoritative-server-rerun/replay-evidence.json` has `pass: true` and no replay
failure lines. The client/network sources above are byte-identical to session baseline
`ca238325f4064aa33f004bb97881674a1ccf1be1`; the omission predates the active navigation session.

## Design

Keep this debt fix in the client network load/reset and receive-classification boundary. Use the existing
server-assigned slot epoch as the identity signal; do not add a protocol field or alter server epoch
allocation.

1. At the load boundary, clear every transient coordinate receive buffer that can be applied after reset,
   including static data. Add one client-owned barrier record per slot with `bArmed`, the pre-load coordinate,
   the last server epoch known for that slot, and a FIFO list of deferred raw full/static packets; arm it before
   `ResetAllSlots` overwrites the slot state. Keep `mDelayedPackets` intact; every delayed packet is classified
   through the same barrier when it releases, so a valid post-load packet that crossed the control channel is
   never removed by a blanket queue purge.
2. While an armed barrier is active, reject a coordinate packet with the stored pre-load epoch before the
   existing full/static/update classifiers run. A full or static packet with a different epoch and the target
   coordinate is retained in that slot's FIFO until the new `SubscribeAccept`; this applies before acceptance
   while the slot is `kSubscribing` or `kUnsubscribed`. Packets for another coordinate, malformed slot identity,
   or an update/resend are dropped by the existing reject path. The normal non-load `kUnsubscribed` static-data
   path, coordinate/ghost and cancellation behavior, and update/resend epoch checks resume after the barrier is
   cleared.
3. When `SubscribeAccept` arrives for an armed slot, it is the only operation that installs the new epoch and
   clears that barrier. It discards deferred full/static packets whose epoch or coordinate does not match the
   accepted `(slot, coord, epoch)`, then replays each matching packet exactly once through the normal receive
   classifiers in the same poll drain; a matching full state activates the slot and matching static data stays
   in the receive buffer for that drain's game-layer hydration. Thus channel 0 and channel 2 may release in
   either order without losing valid post-load traffic or admitting the pre-load epoch.
4. Keep the change client-owned: do not modify `NetworkSimulation`, its per-channel FIFO/release behavior, the
   server slot allocator, packet layouts, or protocol fields. The implementation must use the existing server
   slot epoch as the identity signal and only add the narrow barrier/deferred-packet state needed by the client.

## Critical files

- `Engine/Source/Network/Client/ClientSessionRuntime.cpp` / `.h` — `ResetForServerLoad`, load-notification
  drain ordering, and client-owned reset state.
- `Engine/Source/Network/Client/Client.cpp` / `.h` — `Poll`, `ResetAllSlots`, the per-slot load barrier and
  deferred pre-accept packets, plus transient full/static/update buffers.
- `Engine/Source/Network/Client/ClientReceive.cpp` — barrier-aware `ClassifyFullState`,
  `ClassifyCoordUpdate`, `ServerCoordFullState`, `ServerCoordStaticData`, and `ServerSubscribeAccept` replay
  of matching deferred packets.
- `Engine/Source/Network/NetworkSimulation.h` — delayed per-channel queue and slot purge helpers, read-only;
  no simulation-queue changes are part of this Plan.
- `Engine/Source/Network/NetworkManager.h` — control versus coordinate channel mapping, read-only.
- `Engine/Source/Network/Server/ServerSend.cpp`, `Engine/Source/Network/Server/Server.h`, and
  `Engine/Source/Network/Server/ServerReceive.cpp` — notification/channel and epoch-reuse contracts,
  read-only; the server implementation is not changed by this Plan.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSessionReceive.cpp` — static/full hydration
  order, read-only evidence for the no-stale-application invariant.

## In scope

- Client load/reset state in `ClientSessionRuntime::ResetForServerLoad`, including the per-slot pre-load
  coordinate/epoch barrier, retained delayed-packet classification, and clearing of all transient coord
  receive buffers that can survive the reset.
- Client slot reset identity needed to reject pre-load packets after a server slot is reused, while preserving
  the existing server epoch wire field and normal slot-reuse/ghost behavior.
- Client full-state, static-data, and update/resend receive classification plus the per-slot deferred
  pre-accept full/static list needed to enforce the load epoch barrier without breaking valid cross-channel
  subscription/full-state ordering.
- Narrow client-owned documentation updates only if the durable receive/reset invariant changes.

## Out of scope

- `Documents/Plans/Frame/NavInflateMarginUvRelative.md` and every active navigation source or acceptance
  measurement.
- Server save/replay implementation, `DifferenceStreamReader`, authoritative CRC generation, or server
  subscription/epoch allocation; server replay remains an independent acceptance signal.
- Any wire-layout, protocol-version, `Frame::kiVersion`, save/replay-format, or backward-compatibility change;
  no new packet field or server message is added.
- `ClientReceiveStreamStateGate.md`'s truncated-stream/deserialization checks; malformed-payload rejection is
  a separate residual even though both plans touch `ClientReceive.cpp`.
- Game reconciliation, rollback policy, `FrameStaticData`/`Frame` serialization, island hydration semantics,
  connection teardown, generic queue redesign, and unit tests.

## Risk tier and invariants

**Change Workflow Tier 3** — triggers: network/replay state boundary, client/server epoch and wire semantics,
cross-channel delayed transport ordering, and shared CRC/determinism consequences. The implementation must
preserve these invariants:

- No pre-load static/full/update packet can populate the newly reset client slot, received buffer, snapshot
  ring, or game hydration path, regardless of whether it releases before or after the load notification.
- A valid post-load packet for the newly allocated server epoch is admitted exactly once even when control
  channel 0 and coordinate channel 2 release in either order; existing placeholder, ghost-unsubscribe,
  cancellation, and resync behavior remains valid.
- Server-assigned epoch meaning and all packet layouts remain unchanged. Reliable per-channel FIFO and the
  deterministic network simulation remain unchanged.
- The server authoritative replay path remains bit-deterministic and its CRC/checksum evidence is unaffected;
  client confirmed CRCs contain no stale-load contribution after the fix.
- Poll/drain ordering and client main-thread ownership remain intact; no new unit-test-only path substitutes
  for live verification.

## Acceptance criteria

1. Structural inspection and targeted static checks prove `ResetForServerLoad` clears transient coord receive
   outputs, retains `mDelayedPackets` for barrier classification, rejects the stored pre-load epoch, and proves
   every full/static/update classification path cannot change the post-load epoch/ring from a pre-load packet.
2. A live Debug client/server run with `keNetworkSimulation == kChina` performs a server load or replay load
   while a coordinate subscription is active and lets delayed channel-0/channel-2 traffic release in both
   observed orders. No stale packet is applied to the new epoch, no confirmed CRC/desync line appears, and
   the intended coordinate reaches `kActive` with static data and a full state hydrated.
3. The same kChina scenario demonstrates that a valid post-load full/static packet arriving before
   `SubscribeAccept` is deferred per slot, replayed exactly once after the matching accept installs the new
   epoch, and is not lost; normal subscription, resync, and update ACK progress continues afterward.
4. The authoritative server replay evidence still reaches its replay loop with zero checksum/replay failure
   lines (`Temp/NavInflateFinal100/F-authoritative-server-rerun/replay-evidence.json` pattern), independent of
   client transport observations.
5. Debug x64 client and server builds compile and link. The final diff shows no wire/protocol/version or
   navigation-plan changes.

## Verification

Use `/agent-harness` for the live kChina load/replay scenario, packet-order/release observations, subscription
state and CRC/desync logs, and clean shutdown. Use `/compile` for Debug x64 client and server builds. Re-run
the server-only replay evidence check separately; a client failure must not be relabeled as an authoritative
server replay failure.

## Coordination

No directional dependency is required. `Documents/Plans/Network/ClientReceiveStreamStateGate.md` is adjacent
work in the same receive file but owns silent truncated-stream rejection, not load epochs; if both plans are
implemented in one session, rebase the second change and rerun both receive-path checks without combining their
root causes. The navigation Plan remains independent and is not edited or co-landed by this debt fix.

## Notes

- This Plan records a proven pre-existing/out-of-scope residual; it does not claim that the active navigation
  geometry caused the client transport race.
- The implementation stores the pre-load coordinate and epoch in a client-owned per-slot barrier, retains
  delayed packets for receive-time classification, and defers only non-old full/static packets until the
  matching `SubscribeAccept`; no wire field, server behavior, or existing non-load out-of-order path changes.
