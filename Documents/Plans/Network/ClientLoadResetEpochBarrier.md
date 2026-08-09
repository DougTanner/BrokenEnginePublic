<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-03T21:48:51.733Z","dependsOn":[]} -->
# Prevent Client Load Reset From Admitting Stale Coord Packets

## Context

This is a pre-existing Tier-3 client transport defect at the load/reset and coordinate-subscription
boundary. It is independent of navigation geometry, authoritative server replay, and the completed stream
validation work. The implementation is client-owned; the server allocator, packet layouts, protocol version,
and transport simulation remain unchanged.

The current code has a reachable cross-channel race:

- `ClientSessionRuntime::ResetForServerLoad` (`Engine/Source/Network/Client/ClientSessionRuntime.cpp:158-168`)
  resets the clock and subscription state, calls `Client::ResetAllSlots`, clears received full states and
  updates, but does not clear `mReceivedStaticData`.
- `Client::ResetAllSlots` (`Engine/Source/Network/Client/Client.cpp:98-105`) overwrites every slot and
  cancelled-subscription record, so no client-side pre-load coordinate/epoch identity survives the reset.
- `Client::Poll` (`Engine/Source/Network/Client/Client.cpp:136-205`) clears transient receive outputs,
  services ENet events, and then releases `mDelayedPackets`. `PollAndDrain` consumes the load flag and resets
  at `Engine/Source/Network/Client/ClientSessionRuntime.cpp:215-223`, before applying static data, full states, and updates. The
  `ServerLoadNotification` receive handler (`Engine/Source/Network/Client/ClientReceive.cpp:569-577`) must
  arm a same-Poll barrier immediately. A coordinate packet released before the notification can therefore
  be present in an output buffer when the reset runs, while one released after the notification is quarantined
  before it can enter the new slot state.
- Control reliable traffic uses channel 0 (`Engine/Source/Network/NetworkManager.h:15-25`), while static/full
  traffic uses the per-slot reliable channel (`Server::SendCoordStaticData` and
  `Server::SendCoordFullState`, `Engine/Source/Network/Server/ServerSend.cpp:13-77`).
  `NetworkSimulation::EnqueueOrDrop` (`Engine/Source/Network/NetworkSimulation.h:135-187`) preserves FIFO
  per channel, not across channels, so both release orders are reachable.
- `Client::ClassifyFullState` (`Engine/Source/Network/Client/ClientReceive.cpp:73-116`) allows a same-
  coordinate `kSubscribing` packet without an epoch, and `ServerCoordFullState` currently decompresses before
  classification (`Engine/Source/Network/Client/ClientReceive.cpp:159-207`). `ServerCoordStaticData` (`Engine/Source/Network/Client/ClientReceive.cpp:244-301`) accepts
  both `kSubscribing` and `kUnsubscribed` paths without a pre-accept epoch barrier. The epoch is installed
  only later by `ServerSubscribeAccept` (`Engine/Source/Network/Client/ClientReceive.cpp:453-538`).
- The server's current slot reuse contract is read-only evidence: `ClientConnection::FreeSlot` preserves the
  epoch (`Engine/Source/Network/Server/Server.h:99-106`), and `Server::ClientSubscribe` increments it before
  sending the accept (`Engine/Source/Network/Server/ServerReceive.cpp:361-375`).
- The normal server/game ordering is also current read-only evidence: `ServerSession::SendNewSubscriptionFullStates`
  sends static data before full state (`Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp:517-545`),
  and `ClientSessionRuntime::PollAndDrain` drains static data before full states before updates
  (`Engine/Source/Network/Client/ClientSessionRuntime.cpp:215-223`).

No historical investigation artifact or existing temporary replay log is a dependency or acceptance evidence
for this Plan. The implementation must produce fresh fixture and bounded-smoke evidence, and all source citations above refer to
the current tree rather than an earlier session snapshot. The current sources are not byte-identical to the
earlier `ca238325f4064aa33f004bb97881674a1ccf1be1` snapshot; that snapshot is not evidence for this Plan.

## Resolved findings and decisions

The following accepted findings are closed decisions, not alternatives for implementation:

| Finding | Decision recorded in this Plan |
| --- | --- |
| PA-F-001 | A load barrier is enforced on the client receive path. It preserves retired slot/coord/epoch identity, clears every applyable transient output including static data, and classifies packets released later from the existing delayed queue. |
| PA-F-002 | There is no temporary or local placeholder slot. `mCoordSlots` contains only authoritative or real transitional slot state; `SendSubscribe` creates one separate pending candidate per coordinate/client generation. Logical capacity includes candidates and tombstones. |
| PA-F-003 | Retired epoch history is client-owned and connection-lifetime. Epochs are compared by equality, including uint16 wrap; a retired accept is rejected with an epoch-qualified unsubscribe and retry, while exhaustion logs the identity, sets `manualReconnectRequired`, calls the existing disconnect path, and requires manual reconnect. There is no automatic reconnect, discovery, or retry on exhaustion, and no server or wire change. |
| PA-F-004 | Deferred full/static packets are owned client bytes with reader, identity, semantic, and aggregate bounds. The aggregate owned-byte trust ceiling is `common::kiMaxDeserializedBytes` (268435456 bytes; 256 MiB); full declarations also satisfy `kiMaxUncompressedFrameBytes`. No ENet or delayed-queue pointer is retained, and malformed or over-budget input does not evict a candidate or disconnect the peer. |
| PA-F-005 | A candidate holds at most one validated static and one validated full packet. A matching pair is prepared and replayed through the production receive path in static-before-full order exactly once; the deferred store is empty after every terminal path. |
| PA-F-006 | Cancellation, timeout, conflict, reject, late accept, unsubscribe ACK, reset, and disconnect are explicit lifecycle paths. Conflict/cancel/timeout tombstones keep capacity and suppress same-coordinate retry until the specified terminal response; budget rejection has no separate lifecycle state. |
| PA-F-007 | Verification uses a narrow `kbDebugInput` client fixture/query in `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp` over the existing JSON command transport and production receive path with internally owned packet bytes. It is not a generic AgentHarness raw-packet injector, and no `Tools/AgentHarness` change is allowed. |
| PA-F-008 | `ServerLoadNotification` arms a same-Poll barrier immediately. A later same-Poll accept decodes the fixed old identity tagged with the old candidate generation and sends an epoch-qualified unsubscribe without binding or creating a candidate; later same-Poll static/full/update/resend packets are metadata-only quarantine drops before classification, decompression, ACK, or output. Reset clears applyable outputs but retains retired identities and ACK cleanup records; delayed packets released in later polls use retired classification. The server is unchanged. |
| PA-F-009 | Deferred ownership is bounded only by the client aggregate `common::kiMaxDeserializedBytes` (268435456 bytes; 256 MiB) across candidates. Full packets validate `iUncompressedSize <= kiMaxUncompressedFrameBytes`; static packets validate semantic and reader bounds against the same aggregate trust ceiling. The byte count is the complete copied packet size, and same-Poll quarantine retains no variable payload. No server producer bound is added. |
| PA-F-010 | When a slot exhausts its uint16 epoch domain, the client logs the `(slot, coord, epoch)` identity, sets `manualReconnectRequired`, calls the existing disconnect path, and stops. Automatic reconnect, discovery, and retry are forbidden; epoch history restarts only after a manual reconnect. |
| PA-F-011 | There is no budget-rejection lifecycle state or promise object. Malformed type/reader/negative-size input drops with the candidate unchanged; packet/payload/uncompressed/budget failures drop as oversized or budget failures with the candidate unchanged; identical same-identity/type/bytes input is a duplicate drop; same-identity/type different bytes after retention is a third drop with the candidate unchanged; a different slot/coord/epoch is a conflict that discards the pair, tombstones the candidate, unsubscribes known identities, and awaits ACK; absent or retired identities never create state. Only conflict, cancel, timeout, reject, accept-plus-unsubscribe-ACK, reset, and disconnect transition lifecycle state. |
| PA-F-012 | The narrow fixture exposes only `run(order)` and `inspect`. Every run executes the fixed 19-case matrix and returns the exact D0/C/A/S/F/U/R/Q/N/Z/K oracle, both control-before-coordinate and coordinate-before-control sequences, explicit setup/touched-slot state, exact final deltas, and mandatory per-case `case`/`passed`/`candidateGeneration`, capacity, deferred, `staticValidation`, drop (including post-notification), replay, terminal, cleanup-ledger, and retry groups. Valid/stale/malformed/oversized/third cases eventually replay a matching pair once; duplicates replay once with a duplicate counter; conflict/cancel/timeout/reject/late/reset/disconnect paths replay zero with an empty store; same-Poll quarantine drops one accept/static/full/update/resend with zero slot mutation and unsubscribe plus ACK; wrap retries; exhaustion disconnects with manual reconnect; capacity 16 admits and capacity 17 rejects the seventeenth use; cleanup-ledger order and both run orders are equal. |
| PA-F-013 | Runtime verification is bounded: record existing server `status` (`clientCount`, `activeCoords`), client `describe_scene` intended `subscribedCoords` with no stale coordinate, new-fixture `inspect` with bounded counts/bytes and empty store/no mutation, the documented `client_full_state_fixture` `arm_stall`/`inspect`/`exercise_gap`/`clear` sequence with `deferDesync:false`, `adoptionDesync:false`, `pendingCleared:true`, and `directAdoptionProven:true`, and a bounded `get_logs` regex containing expected reset markers with no newly appended error lines. All other preservation is established by diff/static checks. |
| PA-F-014 | Future execution roles are complete for Tier 3: fresh plan audit, external grill, and user approval; two disjoint implementers for the engine client and project fixture/docs; `/update-affected-code`; builders for both Debug x64 targets; fresh `/repo-code-review`, non-C++ coherence, `/scope-review`, and `/adversarial-review`; `/code-style-review`, conditional `/update-vcxproj`, and `/update-claude-docs`; manager-owned `/agent-harness`; then `/finalize-changes` prepares the final diff, a fresh `/verify-changes` reviews it, and one confirmation authorizes land and claim removal. No server implementer, game/server send-or-drain implementer, generic AgentHarness role, or locator is assigned. |
| PA-F-015 | Cleanup is an ordered ledger indexed by the 64 server slots. Each entry is exactly `(slot,epoch,coord,candidateGeneration,reason,sequence)`. Identical identities coalesce; distinct entries remain FIFO; only the oldest entry for a slot is sent, and the next entry is sent only after that entry's ACK. A nonempty per-slot ledger fences local reuse and authoritative mutation. ACK consumes the next ledger entry, and it mutates the current slot only when the ACK identity exactly matches that entry. An ACK with no ledger entry is counted as unmatched/duplicate/stale and never mutates state. Channel-0 FIFO is the ordering guarantee. The ledger has a cap of 64 entries per slot and 4096 total, never evicts; exhaustion fails closed with manual disconnect. Reset preserves ledger and fences; disconnect clears them. Ledger entries never multiply `logicalUse`; fixture telemetry covers sent/coalesced/depth/order, ACK dispositions, fencing/reuse blocking, and overflow. |
| PA-F-016 | Static validation is a non-retaining validator in the future writable `Engine/Source/Network/Client/ClientReceive.cpp` path before any copy, not a change to the `NavData` reader. It consumes the exact static envelope and bounded cursor, checks `islands <= kiMaxIslandsPerCell`, bounded nonnegative V/P/E counts, polygon offsets in `0..V` and monotonic, edge endpoints `< V`, checked arithmetic, and the preparation bound with `G=4096` and `Q<=V` against `common::kiMaxDeserializedBytes`. Copy occurs only after validation; the production reader remains unchanged. The plan names this exact scope and acceptance evidence. |
| PA-F-017 | The fixture oracle is replaced by the exact D0/C/A/S/F/U/R/Q/N/Z/K conventions and 19-case table, with both control-before-coordinate and coordinate-before-control sequences, exact final deltas/states, timeout evidence, setup states, and touched slots. Every case exposes mandatory cleanup-ledger fields `sent`, `coalesced`, `depthPeak`, `acked`, `pending`, `fencedSlots`, `reuseBlocked`, `unmatchedAck`, `duplicateAck`, `staleAck`, `overflow`, and `orderPreserved`; `ledger.overflow` is ledger-cap telemetry only, never the removed candidate lifecycle state. `staticValidation` exposes reason, preparation-byte peak, and retained-byte evidence. |
| PA-F-018 | Bounded smoke follows the exact documented launch/readiness, reset/status choose-active-C/spawn/query, connect-count/subscribed-C, named-save/baseline, paused-load/reset-marker, bounded-unpause/fresh-identity, full-state-stall/timescale/gap, bounded-log, and reset/clear/release recipe recorded in this Plan and in the existing AgentHarness command/verification sections. |
| PA-F-019 | Writable documentation scope is exact: the `Subscription Receive Invariants` section of `Engine/Source/Network/Client/AGENTS.md`, the existing command and verification sections of `Projects/BrokenEngineSandbox/Documents/AgentHarness.md`, and only the conditional import-stub mechanics in `Engine/Source/Network/Client/CLAUDE.md` through `/update-claude-docs`. Vague sibling-document scope is removed. |
| PA-F-020 | Execution order is exact: fresh audit; external grill plus approval; two implementers (engine and project fixture/docs); `/update-affected-code`; builders for both Debug targets; fresh C++ review, non-C++ coherence review, scope review, and adversarial review; style, conditional project membership, and documentation synchronization; manager harness; finalize prepares the diff, fresh verify reviews it, then one confirmation lands and removes the claim. No server, generic harness, or locator role. |
| PA-F-021 | Malformed/oversized/budget/duplicate/third cannot evict live candidate or authoritative/game-visible state; identity conflict deliberately discards only candidate deferred pair, tombstones it, ledger-cleans known identities, never clears unrelated authoritative slot or publishes. |

## Design

### Client-owned candidate and capacity model

Add one client-owned pending-candidate record per coordinate and client generation. A generation is a
client-local monotonically increasing identifier and is not added to any packet. Each record contains the
coordinate, generation, transition time, lifecycle state, cancellation/tombstone state, optional server
identity `(slot, coord, epoch)`, at most one owned validated static packet, at most one owned validated full
packet, and fixture counters. The record owns copied bytes; it never stores an ENet pointer or a span into
`DelayedPacket::data`.

`mCoordSlots` remains the authoritative/transitional slot array. `SendSubscribe` must not create a
`kSubscribing` placeholder merely to reserve a local slot. A legacy `kSubscribing` state that is still
reachable is fail-safe and counts as used. The subscription queue suppresses a coordinate already represented
by an authoritative slot or any pending candidate, including a cancelled/timed-out tombstone. A same-
coordinate retry waits until the old candidate reaches a terminal response.

The capacity invariant is exact:

```text
logicalUse = authoritativeSlots + unboundPendingCandidates
```

`authoritativeSlots` counts every `mCoordSlots` entry in `kSubscribing`, `kWaitingFullState`, `kActive`, or
`kUnsubscribing`; only `kUnsubscribed` is free. An accepted candidate is bound to its server slot and moves
from `unboundPendingCandidates` to the corresponding real transitional slot exactly once, without double
counting. Its private static/full bytes remain attached to that slot's candidate state until the pair is
committed or discarded. Unbound candidates include live, cancelled, timed-out, and conflict tombstones.
Same-Poll quarantine is metadata-only and never creates a candidate or consumes variable-payload budget.
`logicalUse` must never exceed `mCoordSlots.size()`; the sandbox contract is 16 slots. A tombstone occupies
capacity until reject, accept followed by unsubscribe ACK, load reset, or disconnect. `kUnsubscribing` is not
reusable before its ACK.

### Ordered cleanup ledger

Cleanup is separate from candidate capacity. Maintain an ordered cleanup ledger indexed over the 64 server
slots; the local sandbox still has 16 physical coordinate slots. Every ledger entry is exactly
`(slot,epoch,coord,candidateGeneration,reason,sequence)`. The identity key for coalescing is
`(slot,epoch,coord,candidateGeneration,reason)`; an identical identity already present is coalesced, while a
distinct identity is appended in FIFO sequence order. `sequence` is monotonic for the connection and is never
used to infer epoch ordering.

For each server slot, send only its oldest ledger entry. Do not send a later entry until the oldest entry's ACK
has arrived. A nonempty per-slot ledger fences that local slot: no local slot reuse, authoritative bind, or
authoritative mutation can pass the fence. The channel-0 reliable FIFO is the transport ordering guarantee;
the ledger must not depend on cross-channel ordering. An ACK consumes the next ledger entry only after its
identity is checked. It may mutate the current slot only when `(slot,epoch,coord,candidateGeneration)` exactly
matches that entry; an ACK with no ledger entry is telemetry (unmatched, duplicate, or stale) and never mutates
the slot. ACKs for a different identity likewise leave the current slot untouched and are counted as stale.

The cap is 64 entries per server slot and 4096 entries in total. There is no eviction and no replacement of an
older entry. Reaching either cap fails closed: set `manualReconnectRequired`, take the existing disconnect
path, and wait for manual reconnect. Ledger entries do not multiply `logicalUse`, and a fence is not a second
candidate. A load reset preserves every ledger entry and its fence; disconnect clears the ledger and all fences.
Fixture telemetry reports `sent`, `coalesced`, `depthPeak`, `acked`, `pending`, `fencedSlots`, `reuseBlocked`,
`unmatchedAck`, `duplicateAck`, `staleAck`, `overflow`, and `orderPreserved`. Here `overflow` is only
ledger-cap-exhaustion telemetry, not a candidate lifecycle state.

### Load barrier and retired epochs

`ServerLoadNotification` arms a same-Poll barrier immediately in the receive handler, before
`ResetForServerLoad` calls `ResetAllSlots`. Preserve every known per-slot `(coord, epoch)` identity and ACK
cleanup record, and mark any live candidate/tombstone as reset-terminal. Clear the game-visible static-data,
full-state, and update outputs, but retain `mDelayedPackets`; every packet released later still passes the same
receive classifiers. While the barrier is armed for the current poll, a fixed-identity accept tagged with the
old candidate generation sends an epoch-qualified unsubscribe immediately, without binding or creating a new
candidate. Static/full/update/resend packets released later in that same poll are metadata-only quarantine drops:
they do not classify, decompress, ACK, publish output, retain variable payload, or mutate slot/candidate state.
Repeated load resets preserve retired history. `ServerUnsubscribeAck` records the slot identity before clearing
the slot, and every slot-reuse, cancellation, timeout, and late-accept path records a known identity before it is
discarded. Packets released in later polls use retired-identity classification. Disconnect clears candidates and
retired history because it ends the connection lifetime; a new history starts only after manual reconnect.

Maintain bounded per-slot retired epoch history covering the full uint16 domain. Each entry retains the
coordinate for diagnostics and the epoch for membership. Membership is tested by exact equality only; numeric
ordering is never used, so a wrapped value is not considered newer. A full/static/update/resend packet whose
slot epoch is retired is dropped before it can seed a candidate, mutate a slot, enter a receive buffer, or reach
game hydration. A `SubscribeAccept` for a retired epoch is never installed: send the epoch-qualified
unsubscribe, keep the candidate tombstoned until its ACK, then retry the coordinate. When every uint16 epoch
value for a slot is retired, stop retrying on that connection and take the existing client disconnect path, set
`manualReconnectRequired`, and wait for manual reconnect; do not synthesize an epoch, accept a wrapped one, or
automatically reconnect/discover/retry.

### Pre-accept identity and trust-boundary validation

Full/static packets released before their accept route by candidate coordinate, but their identity is the exact
tuple `(server slot, coordinate, epoch)`. The first validated packet establishes the candidate identity; every
later packet and the accept must match it. An old identity, mismatched coordinate, retired epoch, or absent
candidate is invalid and does not create state. If two packets establish different identities, record a conflict,
discard only that candidate's retained static/full pair, tombstone the candidate, ledger-clean every known
identity for it, and fail closed with unsubscribe/retry when a server slot is known. Do not clear an unrelated
authoritative slot or publish any candidate output.

Validate in this order before copying bytes or decompressing: exact packet type; whole packet and encoded payload
size; `NetworkMessages::Read`/`BoundedCursor` reader bounds; slot range; candidate lookup; coordinate and epoch
identity; non-negative declared sizes; full `iUncompressedSize` no greater than `kiMaxUncompressedFrameBytes`;
and static semantic/reader bounds (`Engine/Source/Network/NetworkMessages.h:89-137`, `FrameStaticData::Read`,
`Engine/Source/Frame/FrameStaticData.cpp:24-57`). The aggregate owned-byte budget across all candidates is
`common::kiMaxDeserializedBytes` (268435456 bytes; 256 MiB; `Common/Serialization.h:25-30`). Count complete copied packet bytes
only. A packet or payload that would exceed the aggregate budget is rejected before copying; same-Poll quarantine
retains no variable payload. Validate the static reader and full uncompressed-size prefix before retaining data
or invoking decompression. Copy the complete packet into client-owned bytes only after all checks; never retain an
ENet pointer. No server producer bound is added.

Same-identity duplicate static/full packets are ignored and do not increase deferred or replay counts. An
invalid type/reader/negative-size packet is dropped with the candidate unchanged. A packet/payload/uncompressed
failure is an oversized drop, and an aggregate-budget failure is a budget drop; neither evicts the candidate or
disconnects the peer. A same-identity/type packet with different bytes after retention is a third drop with the
candidate unchanged. A different identity is a conflict: discard the retained pair, tombstone the candidate,
ledger-clean known identities, and await ACK. Every disposition is counted for fixture telemetry. Malformed,
oversized, budget, duplicate, and third input cannot evict a live candidate or authoritative/game-visible state.

### Static validation boundary

The future writable static receive change belongs in `Engine/Source/Network/Client/ClientReceive.cpp`, in a
non-retaining validator that runs before copying any static packet bytes. It is not a change to
`NavData::Read` or any other production reader. The validator consumes the exact static envelope and bounded
cursor: it checks the packet type, envelope length, and every declared field, and it must finish exactly at the
envelope end with no trailing or unconsumed bytes. A failure returns a reason and retains nothing.

The validator checks `0 <= I <= kiMaxIslandsPerCell`, and bounded nonnegative `V`, `P`, and `E` counts. In the
preparation notation, `V` is the vertex count, `P` is the polygon-offset count, `E` is the visibility-edge
count, `Q` is the checked polygon-offset cursor count (`Q = P`), and `G = 4096` is the fixed edge-fanout
bound. It requires `Q <= V`; every polygon offset is in `0..V` and is monotonic; and every edge endpoint is
strictly less than `V`. All additions and multiplications are checked before conversion or cursor advance.
The preparation bound is checked with checked arithmetic as:

```text
Q = P <= V
E <= G * V, G = 4096
BstaticEnvelope = NetworkMessages::ServerCoordStaticDataMessage::kiFixedSize
Bprep = BstaticEnvelope + sizeof(XMVECTOR) + sizeof(int32_t) + I * BIslandPlacement
       + sizeof(int32_t) + V * sizeof(XMFLOAT2)
       + sizeof(int32_t) + Q * sizeof(int32_t)
       + sizeof(int32_t) + E * (sizeof(int32_t) + sizeof(int32_t))
Bprep <= common::kiMaxDeserializedBytes
```

`BIslandPlacement` is the exact serialized placement width (`islandCrc`, `f2WorldPos`, and `fRotation`),
`BstaticEnvelope` is the exact static packet fixed size including its payload-size field, and the formula is
evaluated without unchecked intermediate overflow. The validator's `preparationBytesPeak` records the largest checked bound and
`retainedBytes` records the complete copied packet size only after all checks pass. Copy occurs only after the
validator proves exact cursor consumption and the aggregate `common::kiMaxDeserializedBytes` budget; the
existing production reader remains unchanged and consumes the already validated bytes.

### Accept, replay, and terminal lifecycle

`ServerSubscribeAccept` is the only path that binds a candidate to a real server slot. A reject sentinel clears
the matching candidate and its bytes. A non-sentinel accept must pass slot range, retired-epoch, candidate,
coordinate, and identity checks. A matching non-cancelled accept installs the real slot identity as
`kWaitingFullState` only after those checks; it does not publish static/full output until both exact retained
packets are present. A slot/candidate state conflict follows the existing ghost-unsubscribe path and never
overwrites an active slot. Identity conflict cleanup uses the ordered ledger and never clears an unrelated
authoritative slot.

When both retained packets are available, validate/prepare them, then invoke the normal receive classifiers in
this fixed order: static first, full second. Static must be classified while the slot is still
`kWaitingFullState`, because full state activates the slot and the static path rejects an active slot. The pair
is replayed exactly once; a matching full state then sets ACK/epoch state and activates the slot, and the game
drain receives static data before the full state. Clear both owned packets and the candidate's deferred store
immediately after the terminal replay decision. A cancelled candidate is never replayed. A late accept for a
cancelled or timed-out candidate binds only long enough to send the authoritative epoch-qualified unsubscribe
and waits for the ACK before releasing capacity. A conflicted candidate follows the same known-identity
unsubscribe/ACK cleanup through the ordered ledger; no separate conflict or budget terminal state is introduced.

`CancelSubscription`, `RecoverTimedOutSubscriptions`, `ServerUnsubscribeAck`, load reset, and disconnect must
cover every candidate and slot state. Cancellation and timeout create tombstones rather than freeing a slot;
reject, matching accept-plus-unsubscribe ACK, reset, and disconnect clear them. Malformed, oversized, budget,
duplicate, and third drops leave lifecycle state unchanged. Only conflict, cancel, timeout, reject,
accept-plus-unsubscribe ACK, reset, and disconnect transition lifecycle state. Keep active updates, ACK floor
tracking, resync full-state re-commit, ghost unsubscribe, cancellation ghosts, and the existing client
main-thread ownership unchanged outside this barrier. A timeout records its transition/deadline evidence before
creating the tombstone; the tombstone remains fenced until reject or the exact late-accept cleanup ACK.

### Deterministic client fixture/query

Add one narrow `kbDebugInput` command, `client_load_reset_epoch_fixture`, to the existing client command
dispatcher in `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp`. The command accepts exactly
one of these actions:

```json
{"action":"run","order":"control_before_coordinate"}
{"action":"run","order":"coordinate_before_control"}
{"action":"inspect"}
```

`run` requires a connected accepted agent client and executes the complete fixed matrix internally; callers do
not supply arbitrary packet bytes, slots, epochs, or payloads. `inspect` returns the last result without driving
the fixture. On non-`kbDebugInput` builds the command fails with the same explicit debug-build contract as the
existing replay fixtures.

Each `run` executes these cases in this exact order, with fixed internally owned packet data and production
receive-path calls:

```text
matching_pair, stale_retired, same_identity_duplicate, conflicting_identity, malformed, oversized,
third_same_type, cancel_before_accept, timeout_then_reject, late_accept_after_cancel, reject, unsubscribe_ack,
same_poll_load_notification, reset, disconnect, epoch_wrap_collision, epoch_exhaustion, capacity_16, capacity_17
```

The oracle uses these exact tokens in every result. `D0` is the fixed setup state (slot 0 is
`kUnsubscribed`, epoch 7, coord `[0,0]`, with no candidate, output, or ledger entry); `C` creates the one
client candidate/control subscription; `A` is a subscribe accept; `S` is static data; `F` is full state; `U` is
a coordinate update; `R` is a coordinate resend; `Q` is a same-Poll metadata-quarantine disposition; `N` is
`ServerLoadNotification`; `Z` is a zeroing/terminal clear (reject, reset, or disconnect as qualified); and `K`
is the unsubscribe ACK. A superscript reason (for example `S^retired` or `Q^full`) is part of the token, not an
additional event. `D0` and every token are included in the recorded delta, so an empty delta is never inferred
from a single aggregate `passed` value.

The two transport orders are exact: control-before-coordinate starts `D0,C,A,S,F`; coordinate-before-control
starts `D0,S,F,C,A`. The fixture uses the same fixed packet bytes and production receive-path calls for both;
only the channel-0 control release and the per-slot coordinate release order changes. `R` and `U` are still
included in the same Poll quarantine sequence where the case requires them. `Z^reset` clears applyable output
but preserves retired identities, cleanup ledger entries, and fences; `Z^disconnect` clears connection-lifetime
state. `K` is accepted only through the ordered cleanup ledger.

The result schema is fixed. Each `cases[]` item must contain every group and field below; no case may report only
the aggregate `passed` bit. Numeric values in this example are the fixed matching-pair oracle values; every other
case uses the same fields with its exact table values:

```json
{
  "fixture":"client-load-reset-epoch/v1",
  "order":"control_before_coordinate",
  "cases":[{
    "case":"matching_pair",
    "passed":true,
    "candidateGeneration":1,
    "setup":{"state":"D0","slot0":{"state":"kUnsubscribed","epoch":7,"coord":[0,0]},"candidate":"none","ledgerPending":0,"timeout":{"armed":false,"startTick":-1,"deadlineTick":-1}},
    "oracle":{"controlBeforeCoordinate":["D0","C","A","S","F"],"coordinateBeforeControl":["D0","S","F","C","A"],"finalDelta":"slot0 kUnsubscribed@7 -> kActive@8; S=1,F=1; candidate=none; ledger.pending=0","finalState":"slot0 kActive@8:[0,0]"},
    "capacity":{"before":0,"after":1,"peak":1,"slotCount":16,"withinLimit":true,"seventeenthRejected":false},
    "deferred":{"staticCount":0,"fullCount":0,"bytes":0,"peakBytes":128,"storeEmpty":true},
    "staticValidation":{"checked":true,"accepted":true,"reason":"none","preparationBytesPeak":128,"retainedBytes":128},
    "drops":{"stale":0,"retired":0,"mismatch":0,"noCandidate":0,"invalid":0,"oversized":0,"budget":0,"duplicate":0,"conflict":0,"third":0,"postNotification":{"accept":0,"static":0,"full":0,"update":0,"resend":0}},
    "replay":{"staticCount":1,"fullCount":1,"order":["static","full"],"exactlyOnce":true},
    "ledger":{"sent":0,"coalesced":0,"depthPeak":0,"acked":0,"pending":0,"fencedSlots":0,"reuseBlocked":0,"unmatchedAck":0,"duplicateAck":0,"staleAck":0,"overflow":0,"orderPreserved":true},
    "retries":{"sameCoordinateBlocked":0,"unsubscribeSent":0,"ackReleased":0},
    "terminal":{"kind":"matching_pair","unsubscribe":false,"ack":false,"cleared":true,"disconnect":false,"manualReconnectRequired":false,"automaticReconnect":false},
    "touchedSlots":[{"slot":0,"before":{"state":"kUnsubscribed","epoch":7,"coord":[0,0]},"after":{"state":"kActive","epoch":8,"coord":[0,0]}}]
  }],
  "finalSlots":[{"slot":0,"epoch":8,"coord":[0,0],"state":"kActive"}],
  "finalDeltasEqual":true,
  "orderPreserved":true,
  "passed":true
}
```

For every case, `setup` records the initial slot/candidate/ledger state and the timeout start/deadline when
applicable; `oracle` records both exact release sequences, final delta, and final authoritative state;
`capacity.before`, `capacity.after`, and `capacity.peak` are logical-use counts; `deferred.bytes` and
`deferred.peakBytes` count complete copied packet bytes only; and `deferred.storeEmpty` is true after every
case. `staticValidation.reason` is one fixed reason (`none`, `retired`, `malformed_cursor`, `oversized`,
`budget`, `third`, or `not_applicable`), `preparationBytesPeak` is the checked non-retaining bound, and
`retainedBytes` is zero until copy-after-validation. `drops.invalid` covers malformed type/reader/negative-size
input, `drops.oversized` covers packet/payload/uncompressed-size failures, `drops.budget` covers the aggregate
trust ceiling, and `drops.postNotification` is mandatory even when all five values are zero. `replay.order`
contains only `static` and `full`; `terminal.ack` means the required unsubscribe ACK was observed; and every
`touchedSlots[]` entry includes before/after state, epoch, and coordinate so zero mutation is directly checkable.
Candidate generations are client-local and increase only when the fixture creates a candidate. The mandatory
`ledger.overflow` value is cap-exhaustion telemetry only; it is not a candidate lifecycle state.

The exact 19-case oracle table is:

| Case | Setup and timeout evidence | Control-before-coordinate delta | Coordinate-before-control delta | Exact final state/delta | Touched slots and ledger result |
| --- | --- | --- | --- | --- | --- |
| `matching_pair` | `D0`; no timeout | `D0,C,A,S,F` | `D0,S,F,C,A` | `slot0 kActive@8:[0,0]`; replay `S=1,F=1`; candidate/store empty | `0: kUnsubscribed@7 -> kActive@8`; all ledger fields zero, `orderPreserved:true` |
| `stale_retired` | `D0`; retired `(0,7,[0,0])` is installed before `C` | `D0,C,S^retired,A,S,F` | `D0,S^retired,F,C,A,S,F` | `slot0 kActive@8:[0,0]`; `drops.retired=1`; replay once | `0: kUnsubscribed@7 -> kActive@8`; no ledger entry |
| `same_identity_duplicate` | `D0`; candidate generation 1 | `D0,C,A,S,S^duplicate,F` | `D0,S,S^duplicate,F,C,A` | `slot0 kActive@8:[0,0]`; `drops.duplicate=1`; deferred peak unchanged | `0: kUnsubscribed@7 -> kActive@8`; ledger empty |
| `conflicting_identity` | `D0`; candidate owns identity `(0,8,[0,0])` | `D0,C,S,S^(0,8,[1,0]),A,U,K` | `D0,S,S^(0,8,[1,0]),C,A,U,K` | `slot0 kUnsubscribed@7:[0,0]`; candidate tombstoned then cleared; replay zero | `0` unchanged; ledger `sent=1,acked=1,pending=0,fencedSlots=0,orderPreserved:true` |
| `malformed` | `D0`; candidate generation 1; no timeout | `D0,C,A,S^malformed,S,F` | `D0,S^malformed,S,F,C,A` | `slot0 kActive@8:[0,0]`; `staticValidation.reason=malformed_cursor`; replay once | `0: kUnsubscribed@7 -> kActive@8`; ledger empty |
| `oversized` | `D0`; candidate generation 1; no timeout | `D0,C,A,S^oversized,S,F` | `D0,S^oversized,S,F,C,A` | `slot0 kActive@8:[0,0]`; `drops.oversized=1`; replay once | `0: kUnsubscribed@7 -> kActive@8`; ledger empty |
| `third_same_type` | `D0`; candidate has validated `S` bytes | `D0,C,A,S,S^third,S,F` | `D0,S,S^third,F,C,A,S` | `slot0 kActive@8:[0,0]`; `drops.third=1`; replay once | `0: kUnsubscribed@7 -> kActive@8`; ledger empty |
| `cancel_before_accept` | `D0`; `C` creates generation 1 | `D0,C,Z^cancel` | `D0,C,Z^cancel` | `slot0 kUnsubscribed@7:[0,0]`; candidate/store empty; replay zero | no slot mutation; ledger empty |
| `timeout_then_reject` | `D0`; `C` at tick 10, deadline tick 12; timeout observed at tick 12 before reject | `D0,C,Z^timeout,Z^reject` | `D0,C,Z^timeout,Z^reject` | `slot0 kUnsubscribed@7:[0,0]`; timeout tombstone then reject clears it; replay zero | no slot mutation; ledger empty |
| `late_accept_after_cancel` | `D0`; `C` then cancel tombstone; late `A` carries known `(0,8,[0,0])` | `D0,C,Z^cancel,A,U,K` | `D0,C,Z^cancel,A,U,K` | `slot0 kUnsubscribed@7:[0,0]`; candidate/store empty; replay zero | `0` unchanged; ledger `sent=1,acked=1,pending=0`, fence released |
| `reject` | `D0`; `C` creates generation 1; reject sentinel is received | `D0,C,Z^reject` | `D0,C,Z^reject` | `slot0 kUnsubscribed@7:[0,0]`; candidate/store empty; replay zero | no slot mutation; ledger empty |
| `unsubscribe_ack` | `D0`; setup accepts to `kActive@8` before cleanup | `D0,C,A,S,F,U,K` | `D0,S,F,C,A,U,K` | `slot0 kUnsubscribed@8:[0,0]`; replay once before cleanup; candidate/store empty | `0: kActive@8 -> kUnsubscribed@8`; ledger `sent=1,acked=1,pending=0` |
| `same_poll_load_notification` | `A(3,7,C)` is the active authoritative slot; candidate, ledger, and applyable output state are empty; `N` arms barrier before `Z^reset` | `A(3,7,C),N,Z^reset,Q^accept,Q^static,Q^full,Q^update,Q^resend,U,K` | `A(3,7,C),N,Z^reset,Q^accept,Q^static,Q^full,Q^update,Q^resend,U,K` | `slot3 kUnsubscribed@7:C`; all `drops.postNotification` accept/static/full/update/resend are 1; no output/candidate mutation | `3: kActive@7:C -> kUnsubscribed@7:C` only at reset; ledger `sent=1,acked=1,pending=0`; Q never mutates |
| `reset` | `D0`; setup has active `(0,8,[0,0])` and one unsatisfied cleanup entry | `D0,C,A,S,F,U,N,Z^reset` | `D0,S,F,C,A,U,N,Z^reset` | applyable output cleared; slot identity/ledger fence retained; candidate/store empty | `0: kActive@8 -> kUnsubscribing@8`; ledger `sent=1,acked=0,pending=1,fencedSlots=1,reuseBlocked=1` |
| `disconnect` | `D0`; setup has active slot and one pending cleanup entry | `D0,C,A,S,F,U,N,Z^disconnect` | `D0,S,F,C,A,U,N,Z^disconnect` | disconnected; candidate/store/retired history/ledger/fences empty; replay zero after terminal clear | `0: kActive@8 -> disconnected`; ledger `pending=0,fencedSlots=0`, no eviction before disconnect |
| `epoch_wrap_collision` | `D0`; retired epoch 0 for slot 0; incoming accept wraps to retired value | `D0,C,A^retired,U,K,C` | `D0,A^retired,S,F,C,U,K,C` | slot remains `kUnsubscribed@7`; retry creates next candidate generation; `manualReconnectRequired:false` | `0` unchanged; ledger `sent=1,acked=1,pending=0`; retry is fenced until K |
| `epoch_exhaustion` | `D0`; all 65536 exact epochs for slot 0 retired; exhaustion identity logged | `D0,C,A^exhausted,Z^disconnect` | `D0,A^exhausted,C,Z^disconnect` | disconnected; `manualReconnectRequired:true`, automatic reconnect/discovery/retry false; replay zero | slot history/ledger clear only at disconnect; no unrelated slot touched |
| `capacity_16` | `D0`; candidate generations 1..16 created, no placeholder slots | `D0,Cx16` | `D0,Cx16` | all 16 candidates pending; `logicalUse.before=0,after=16,peak=16`; no seventeenth attempt | no authoritative slot touched; ledger empty, `fencedSlots=0` |
| `capacity_17` | `D0`; generations 1..16 live; generation 17 attempted | `D0,Cx16,C^17_rejected` | `D0,Cx16,C^17_rejected` | first 16 candidates unchanged; `logicalUse=16`; `seventeenthRejected:true`; overflow remains 0 | no authoritative slot touched; ledger empty; physical limit never exceeded |

The two `run` orders must return equal final authoritative slot state, equal per-case replay/drop/terminal
signals, equal cleanup-ledger order and counters, and `oracle.orderPreserved:true`. The fixture must make the
timeout tick/deadline, setup state, and every touched slot observable in JSON. The existing
`Projects/BrokenEngineSandbox/Documents/AgentHarness.md` command and verification sections must document this
schema, both-order recipe, exact table signals, the 16-use lifecycle recipe, and the bounded smoke checks. No
`Tools/AgentHarness` source or generic transport injection is part of this change.

## Critical files

- `Engine/Source/Network/Client/Client.h` — `ClientCoordSlot`, the client-owned pending-candidate and retired-
  epoch records, 64-slot ordered cleanup ledger, deferred-byte/static-validation telemetry, and the narrow
  fixture/query hook; no packet layout changes.
- `Engine/Source/Network/Client/Client.cpp` — `CancelSubscription`, `ResetAllSlots`,
  `RecoverTimedOutSubscriptions`, `Poll`, delayed-packet dispatch, and candidate/retired-history lifecycle.
- `Engine/Source/Network/Client/ClientSend.cpp` — `SendSubscribe` capacity/candidate creation and
  `SendUnsubscribe` terminal/retry behavior; only `kUnsubscribed` is reusable.
- `Engine/Source/Network/Client/ClientReceive.cpp` — `Receive`, `ClassifyFullState`, `ClassifyCoordUpdate`,
  `ServerCoordFullState`, `ServerCoordStaticData`, `ServerCoordUpdateOrResend`, `ServerSubscribeAccept`,
  `ServerUnsubscribeAck`, and `ServerLoadNotification` same-Poll quarantine, the future non-retaining static
  validator before copy, cleanup-ledger ACK/fence checks, replay, and telemetry. `NavData::Read` is read-only.
- `Engine/Source/Network/Client/ClientSessionRuntime.cpp` / `.h` — `ResetForServerLoad`, `PollAndDrain`,
  `ResetForConnect`, `Disconnect`, `SynchronizeSubscriptions`, `BuildSubscriptionQueue`,
  `UnsubscribeStaleCoords`, `TrySubscribeNext`, and candidate-aware retry plus manual-reconnect exhaustion
  orchestration.
- `Engine/Source/Network/NetworkMessages.h`, `Engine/Source/Network/NetworkCursor.h`,
  `Engine/Source/Network/NetworkProtocol.h`, and `Engine/Source/Frame/FrameStaticData.cpp` — current reader,
  framing, uncompressed-size, and static-reader contracts; read-only.
- `Engine/Source/Network/NetworkManager.h` and `Engine/Source/Network/NetworkSimulation.h` — channel mapping,
  ownership, and per-channel FIFO/release behavior; read-only.
- `Engine/Source/Network/Server/ServerSend.cpp`, `Engine/Source/Network/Server/Server.h`, and
  `Engine/Source/Network/Server/ServerReceive.cpp` — server static/full send, slot allocation, epoch increment,
  and unsubscribe ACK contracts; read-only evidence.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — static-before-full send/drain
  evidence; read-only.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSessionReceive.cpp` — static-before-full game
  hydration and active-frame semantics; read-only.
- `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsServer.cpp:79-100` — existing server `status`
  `clientCount` and `activeCoords` query; read-only smoke evidence.
- `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp` — the narrow debug fixture/query and
  exact JSON validation/dispatch.
- `Projects/BrokenEngineSandbox/Documents/AgentHarness.md` — existing command and verification sections for
  the fixture schema, deterministic recipe, expected signals, and bounded smoke procedure.
- `Engine/Source/Network/Client/AGENTS.md` — the writable `Subscription Receive Invariants` section for the
  durable receive/reset, cleanup-ledger, and validator invariants.
- `Engine/Source/Network/Client/CLAUDE.md` — only the conditional import-stub mechanics, and only when
  `/update-claude-docs` determines synchronization is required.

## In scope

- `ClientCoordSlot` and client-owned pending-candidate/retired-epoch state in `Engine/Source/Network/Client/Client.h`, including generation,
  transition, cancellation/tombstone, identity, owned static/full bytes, aggregate-byte accounting, and
  fixture telemetry fields.
- `Client::SendSubscribe`, `Client::SendUnsubscribe`, `Client::CancelSubscription`,
  `Client::RecoverTimedOutSubscriptions`, and `ClientSessionRuntime::UnsubscribeStaleCoords` /
  `SynchronizeSubscriptions` / `BuildSubscriptionQueue` / `TrySubscribeNext`: one candidate per coordinate
  generation, exact logical-use accounting, same-coordinate suppression, no placeholder slot, and terminal
  retry behavior with explicit manual-reconnect exhaustion.
- `ClientSessionRuntime::ResetForServerLoad`, `PollAndDrain`, `ResetForConnect`, and `Disconnect`, plus
  `Client::ResetAllSlots` and `Client::Poll`: preserve retired identity, clear all applyable static/full/update
  outputs, retain delayed packets for classification, and clear candidates only on defined reset/disconnect
  paths.
- `Client::Receive`, `ClassifyFullState`, `ClassifyCoordUpdate`, `ServerCoordFullState`,
  `ServerCoordStaticData`, `ServerCoordUpdateOrResend`, `ServerSubscribeAccept`, `ServerUnsubscribeAck`, and
  `ServerLoadNotification`: retired-epoch rejection, candidate identity matching, trust-boundary size/reader
  checks, the future non-retaining static envelope/cursor validator before copy (with `I <=
  kiMaxIslandsPerCell`, bounded nonnegative V/P/E, `Q <= V`, `G = 4096`, checked arithmetic, and the
  preparation bound), same-Poll metadata quarantine, owned-byte storage, conflict/duplicate/third handling,
  accept binding, static-before-full exact-once replay, and terminal cleanup.
- The ordered cleanup ledger indexed over 64 server slots: exact entry identity, coalescing, per-slot FIFO send
  depth, channel-0 sequencing, fences, exact-identity ACK consumption, unmatched/duplicate/stale ACK telemetry,
  64-per-slot/4096-total fail-closed caps, reset preservation, and disconnect clearing. Ledger state never
  multiplies `logicalUse`.
- Client fail-closed retry after retired/wrapped accept, including disconnect plus a manual-reconnect requirement
  when a slot has exhausted all uint16 epoch values, without changing server allocation, packet fields, protocol
  version, or wire behavior.
- The `kbDebugInput` `client_load_reset_epoch_fixture` command/query in the existing project client command
  file, using fixed internal scenarios and the production receive path; its documented JSON schema and
  deterministic recipes in `Projects/BrokenEngineSandbox/Documents/AgentHarness.md`.
- The `Subscription Receive Invariants` section in `Engine/Source/Network/Client/AGENTS.md`, the existing
  command and verification sections in `Projects/BrokenEngineSandbox/Documents/AgentHarness.md`, and the
  conditional import-stub-only synchronization of `Engine/Source/Network/Client/CLAUDE.md` through
  `/update-claude-docs`.

## Out of scope

- Any server source modification: allocator/free behavior, epoch increment, static/full send order, replay
  writer/reader, save/load, ACK generation, or authoritative CRC behavior.
- Any `NetworkMessages` packet layout, `PacketType`, protocol-version, `Frame::kiVersion`, save/replay format,
  compatibility, or wire-field change.
- Any `NetworkManager` channel/reliability rule, `NetworkSimulation` delay/loss/FIFO algorithm, delayed-queue
  purge redesign, or ENet transport change.
- Any generic raw-packet injection command, `Tools/AgentHarness` change, unit test, or test-only receive path
  that bypasses production classification.
- Game hydration, `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSessionReceive.cpp`, frame/snapshot/ring/reconciliation policy, navigation data,
  save/game hydration semantics, active update/ACK/resync/ghost behavior, and connection teardown policy beyond
  the client-owned fail-closed disconnect plus manual-reconnect signal.
- Historical investigation files, absent temporary evidence, and unrelated navigation or stream-validation
  Plans; they are not dependencies, citations, or implementation scope.
- Any sibling documentation outside the named `Client/AGENTS.md`, existing `AgentHarness.md` command/
  verification sections, and conditional `Client/CLAUDE.md` import stub.

## Risk tier and invariants

**Change Workflow Tier 3** — triggers: cross-channel delayed transport ordering, client/server slot epoch
identity, load/reset lifecycle, bounded hostile packet retention, manual reconnect on epoch exhaustion, and
deterministic client/server CRC consequences.

The implementation must preserve these invariants:

- No pre-load static/full/update packet can populate a reset client slot, `mReceivedStaticData`,
  `mReceivedFullStates`, the update ring, or game hydration, regardless of release order.
- The load notification arms a same-Poll barrier immediately: the later same-Poll accept is unsubscribed without
  binding or candidate creation, and later same-Poll static/full/update/resend packets are metadata-only drops
  before classification, decompression, ACK, output, or state mutation. Delayed packets released in later polls
  use the retained retired-identity classification.
- Only an exact current `(slot, coord, epoch)` can enter an authoritative slot. A retired epoch is always dropped;
  a retired/wrapped accept is unsubscribed and retried, and a fully exhausted epoch domain disconnects with
  `manualReconnectRequired` and no automatic reconnect, discovery, or retry.
- `logicalUse = authoritativeSlots + unboundPendingCandidates` is never above the physical slot count, with
  all transitional states and tombstones counted and no accepted candidate double-counted.
- Deferred data is bounded by the aggregate `common::kiMaxDeserializedBytes` budget, fully owned,
  reader/semantic-validated, and cleared on every terminal path. Full declarations remain within
  `kiMaxUncompressedFrameBytes`; static data uses the non-retaining ClientReceive validator with exact envelope/
  cursor consumption, `I <= kiMaxIslandsPerCell`, bounded nonnegative V/P/E, monotonic `0..V` polygon offsets,
  `<V` edge endpoints, checked arithmetic, and the `G=4096,Q<=V` preparation bound. The production reader is
  unchanged. No malformed, oversized, over-budget, conflicting, duplicate, or third packet can evict valid
  state or force a disconnect.
- Malformed/oversized/budget/duplicate/third cannot evict live candidate or authoritative/game-visible state;
  identity conflict deliberately discards only candidate deferred pair, tombstones it, ledger-cleans known
  identities, never clears unrelated authoritative slot or publishes.
- A candidate never publishes static/full data until both exact packets are available; replay is static first,
  full second, exactly once, and the deferred store is empty afterward.
- Malformed, oversized, budget, duplicate, and third drops leave lifecycle state unchanged. Conflict, cancel,
  timeout, reject, accept-plus-unsubscribe ACK, reset, and disconnect are the only lifecycle transitions;
  matching accept/replay only performs the binding and apply operation already specified. Cancelled candidates
  never replay and late accepts unsubscribe through ACK.
- Existing active update, ACK, resync, ghost, cancellation, main-thread, transport/FIFO, navigation, frame,
  save, and game hydration semantics remain unchanged outside the barrier.
- Cleanup ledger entries are ordered per server slot, coalesced only for identical identities, fenced while
  pending, consumed only by exact identity ACK, bounded at 64 per slot/4096 total without eviction, preserved by
  reset, and cleared by disconnect. Channel-0 FIFO is the only ordering guarantee; ledger state never multiplies
  `logicalUse`.
- Server allocator/send/replay behavior, all packet layouts and versions, and authoritative replay determinism
  remain unchanged. The fixture is debug-gated and deterministic and cannot become a generic injection surface.

## Acceptance criteria

1. Structural inspection and focused static checks prove that `ServerLoadNotification` arms the same-Poll
   barrier before reset, the fixed old-generation accept is unsubscribed without bind/candidate creation, later
   same-Poll static/full/update/resend packets are metadata-only drops before classification/decompression/ACK/
   output, reset clears applyable outputs while retaining retired identities, ordered cleanup-ledger entries, and
   fences, and later polls use retired classification. The cleanup ledger is indexed over 64 server slots,
   coalesces identical identities, preserves distinct FIFO order, sends only the oldest per slot, and consumes
   only exact-identity ACKs; channel-0 FIFO is the ordering guarantee.
2. Structural checks prove `logicalUse = authoritativeSlots + unboundPendingCandidates`, the 16-slot limit,
   no-placeholder candidate creation, same-coordinate suppression, tombstone accounting, exact uint16 equality,
   manual-reconnect exhaustion, cleanup-ledger fencing and cap behavior, client main-thread ownership, and no
   automatic reconnect/discovery/retry. Ledger entries do not multiply `logicalUse`; reset preserves them and
   disconnect clears them. Cap exhaustion is fail-closed manual disconnect.
3. Structural trust-boundary checks prove exact type/envelope/cursor consumption, slot/coord/candidate/epoch
   matching, nonnegative sizes, full `kiMaxUncompressedFrameBytes` validation, and the future static validator
   in writable `ClientReceive.cpp` before copy (not `NavData::Read`): `islands <= kiMaxIslandsPerCell`, bounded
   nonnegative V/P/E, `Q<=V`, monotonic polygon offsets in `0..V`, edge endpoints `<V`, checked arithmetic,
   `G=4096` preparation bound, and `Bprep <= common::kiMaxDeserializedBytes`. Copy occurs only after validation;
   production reader stays unchanged. Aggregate ownership counts complete copied packet bytes, with no
   same-Poll variable payload retention and no decompression before validation. No old per-packet,
   per-candidate, or aggregate producer limit is introduced.
4. A fresh Debug `kbDebugInput` client accepts only `run(order)` and `inspect`; each run returns all 19 cases in
   the fixed order, the exact D0/C/A/S/F/U/R/Q/N/Z/K oracle, both release sequences, exact final deltas/states,
   setup and timeout evidence, every mandatory per-case schema group/field, bounded deferred counts/bytes/peak/
   store state, `staticValidation.reason`/`preparationBytesPeak`/`retainedBytes`, every drop counter including
   `postNotification`, replay order/exactly-once, all ledger fields (`sent`, `coalesced`, `depthPeak`, `acked`,
   `pending`, `fencedSlots`, `reuseBlocked`, `unmatchedAck`, `duplicateAck`, `staleAck`, `overflow`, and
   `orderPreserved`), terminal flags, and touched-slot before/after state/epoch/coord. `ledger.overflow` is only
   cap telemetry, not a candidate lifecycle state. Both orders have equal final authoritative state and signals.
5. The exact 19-case table passes: valid/stale/malformed/oversized/third cases eventually replay a matching pair
   once; duplicate replays once with its counter; conflict/cancel/timeout/reject/late/reset/disconnect paths
   replay zero with an empty store and exact ledger behavior; same-Poll quarantine counts one accept/static/full/
   update/resend (`postNotification.resend == 1`) with zero classifier/decompress/ACK/output/slot/candidate mutation before its exact
   ledger unsubscribe plus ACK; wrap retries; exhaustion disconnects with manual reconnect; and capacity 16/17
   proves the seventeenth use is rejected without exceeding 16.
6. Bounded smoke follows the exact documented recipe: launch server/client with the documented readiness checks;
   `reset`, read `status`, choose active coord `C`, `spawn_players`, and query it; connect and require
   `status.clientCount:1` plus client `describe_scene.subscribedCoords:[C]`; save as
   `client_load_reset_epoch_smoke` and capture server/client log baselines; load it with `pauseAfterLoad:true`
   and require `resetToFresh:false`, `paused:true`, old `C` absent, and exact `ServerLoadNotification`/
   `ResetForServerLoad`/retired markers; unpause and require within 64 polls/30 seconds that `C` reactivates with
   a fresh identity; arm `client_full_state_fixture`, call `timescale` faster exactly once (2/1), inspect within
   32 polls/15 seconds and require pending full-state tick above client tick, call `exercise_gap` and require
   exactly `deferDesync:false`, `adoptionDesync:false`, `pendingCleared:true`, `directAdoptionProven:true`, then
   `clear` and call `timescale` slower exactly once (1/1); use a bounded log regex for expected markers and no
   newly appended errors; finish with reset, fixture clear, and harness release. All other preservation claims
   are checked by diff/static inspection only.
7. Writable documentation is limited to `Client/AGENTS.md`'s `Subscription Receive Invariants`, the existing
   command/verification sections of `AgentHarness.md`, and conditional `Client/CLAUDE.md` import-stub mechanics
   through `/update-claude-docs`; no vague sibling documentation is changed. The final diff has no server,
   wire/protocol/version, transport, navigation, generic `Tools/AgentHarness`, or unrelated documentation change.
8. Debug x64 client and server builds compile and link through `/compile`; no unit tests are added. The final diff
   contains no server, wire/protocol/version, transport, navigation, generic `Tools/AgentHarness`, or unrelated
   documentation change, and every changed region is covered by this Plan's concrete scope.

## Verification

The manager owns `/agent-harness` and runs the fixture twice, once with
`control_before_coordinate` and once with `coordinate_before_control`, asserting the exact 19-case D0/C/A/S/F/
U/R/Q/N/Z/K matrix, exact final deltas/states, setup and timeout evidence, all mandatory JSON groups/fields,
static-validation reasons and preparation/retained bytes, equal final authoritative state, cleanup-ledger
coalescing/FIFO/depth/fence/ACK/order signals, same-Poll post-notification counters, retired/wrap/conflict
dispositions, manual-reconnect and ledger-cap exhaustion, and the 16/17 capacity boundary.

The bounded smoke is exact and time-limited. Follow the documented launch and readiness checks; `reset`, read
server `status`, choose active coord `C`, `spawn_players`, and query `C`; connect and require
`status.clientCount == 1` and client `describe_scene.subscribedCoords == [C]`; save as the named
`client_load_reset_epoch_smoke` file and capture server/client baselines. Load with `pauseAfterLoad:true` and
require `resetToFresh:false`, `paused:true`, old `C` absent, and exact `ServerLoadNotification`,
`ResetForServerLoad`, and retired markers. Unpause and require within 64 polls or 30 seconds that `C` is active
again with a fresh identity. Arm `client_full_state_fixture`, call `timescale {"faster":true}` exactly once
and require `2/1`, inspect within 32 polls or 15 seconds and require pending full state above the client tick,
call `exercise_gap` and require exactly `deferDesync:false`, `adoptionDesync:false`, `pendingCleared:true`,
and `directAdoptionProven:true`, call `clear`, then call `timescale {"faster":false}` exactly once and require
`1/1`. Capture a bounded appended log slice with the regex
`ServerLoadNotification|ResetForServerLoad|retired|manualReconnectRequired|CRC|[Dd]esync|[Ee]rror`, require
the expected markers and no newly appended errors, then reset, clear the fixture, and release the harness.
No generic AgentHarness source or broad replay/load scenario is used; all other preservation is checked by
diff/static inspection only.

Use `/compile` for Debug x64 client and server builds. Focused read/search/static checks cover packet type and
reader order, negative-size handling, aggregate byte accounting, same-Poll quarantine, capacity/tombstone
transitions, retired-history equality and exhaustion, candidate ownership, and final diff scope. Do not add unit
tests or modify the generic harness transport.

## Coordination

Future execution is Tier-3 coordinated as follows:

1. Run a fresh `/plan-audit`; then run `/external-grill-plan` and obtain the required user approval before
   implementation.
2. Dispatch exactly two disjoint implementers: one owns only the engine client runtime/receive slice
   (`Engine/Source/Network/Client/Client.h`, `Engine/Source/Network/Client/Client.cpp`,
   `Engine/Source/Network/Client/ClientSend.cpp`, `Engine/Source/Network/Client/ClientReceive.cpp`, and
   `Engine/Source/Network/Client/ClientSessionRuntime.cpp/.h`); the other owns only
   the project `kbDebugInput` fixture and its `Projects/BrokenEngineSandbox/Documents/AgentHarness.md` schema and
   bounded recipes. Neither implementer changes server/game send or drain code.
3. After the C++ changes, an implementer runs `/update-affected-code` for every dependent caller, producer,
   consumer, mirror, and named client documentation site. Builders run `/compile` for both Debug x64 client and
   server targets.
4. Fresh reviewers run `/repo-code-review` for C++, a non-C++ coherence review for the Plan/fixture/docs,
   `/scope-review` once over the whole diff, and `/adversarial-review`. A mechanic runs `/code-style-review`;
   `/update-vcxproj` is conditional on file-membership changes; and an implementer runs `/update-claude-docs`
   for the named C++ documentation scope.
5. The manager owns `/agent-harness` bounded verification. `/finalize-changes` then prepares the final diff;
   a fresh `/verify-changes` reviews that prepared diff; one explicit confirmation authorizes land, claim removal,
   and no other landing action. No server implementer, generic AgentHarness role, or locator is assigned, and
   server/game send-or-drain files remain read-only evidence.

## Notes

- The load notification is a client barrier signal, not a new epoch source. The existing server-assigned epoch
  remains the only wire identity signal.
- A pending candidate is a bounded client lifecycle record, not a local slot placeholder. Its generation is
  diagnostic/client-local only; server slot/coord/epoch remains the authoritative packet identity.
- The final acceptance evidence must be fresh and order-sensitive. Absence of an old log line, a single aggregate
  `passed` field is not sufficient evidence for the client barrier.
