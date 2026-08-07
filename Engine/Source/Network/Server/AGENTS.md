# Network Server - Host, Slots, and Resends

Server-only engine transport. `Server` (`gpServer`) owns the ENet host, client transport records, subscription slots, update rings, and resend state. `ServerSessionRuntime` owns host lifetime, fixed-tick pacing, discovery, poll/tick/paused sequencing, persistent queue servicing, broadcast consumption, resend, flush, and transport reset. The concrete game server session (`../../../../Projects/BrokenEngineSandbox/Source/Network/Server/AGENTS.md`) supplies authorization, broadcast construction, transfers, and gameplay hooks.

## Affinity and Lifetime

- This directory is server-project-only and every source/header remains whole-file `BT_SERVER` guarded.
- ENet host service, compression scratch, queues, and delayed-packet simulation are main-thread-owned.
- Disconnect purges delayed packets for that peer before its transport state can be reused.

## Session Invariants

- Accept a client only after protocol, Frame version, and ordered island-manifest identity pass. Repeated Hello preserves the stored GUID; mint a GUID only for a first accepted client that supplied none.
- Run the parent client-to-server contract gate before dispatch. Variable payload handlers parse into bounded local state before mutation, and contract violations use the central accounting/disconnect path.
- `RecordContractViolation` can destroy the client record: on reaching `kiContractViolationDisconnectCount` it pushes the pending disconnect itself, so the game layer still gets the notification and can keep that client's fleet state, then disconnects the peer and removes the record. Treat the call as the last use of that `ClientConnection*` — read whatever you still need before it and return immediately after, or the next line reads freed memory on a path hostile client input can reach.
- Each client independently admits one desync report and one debug-frame request per two seconds of `steady_clock` time after contract validation. Immediate repeats within the per-update cap are silent; over-cap traffic retains the contract-violation policy. A disabled-build debug-frame request remains contract-valid, starts its cooldown, then returns without parsing, logging, lookup, compression, or sending.
- Each update polls twice: once at update start and once at the tick boundary, after the fixed-tick wait, so commands that arrived during the wait enter the imminent tick. The boundary poll deliberately omits `BeforeNetworkPoll` — that hook clears the previous update's pending request queues, so running it again would drop what the first poll queued — and it continues the first poll's admission budget window, giving each client one budget window per update rather than one per poll.
- Poll-scoped requests drain each poll: fleet request queues are drained by the handler that processes them, so a second poll in the same update cannot reprocess them. New-subscription and resync requests persist until serviced by post-tick broadcast or the paused/zero-tick service path; deduplicate them while pending.
- A subscription must be origin or adjacent to an authorized coordinate. Subscription, ACK, and resend-log bookkeeping stay together per slot; allocation stays within the client-supported count, and reuse increments rather than resets the epoch (the reuse counter).
- After existing-client lookup, unsubscribe ACKs every syntactically valid request and frees only an in-range active slot whose epoch matches the request.
- Clamp ACK floors to buffered history. Treat an all-zero ACK field as latency, not a resend gap, and cap resends per slot per tick.

## Buffered State

- Buffer exactly one delta entry per active coordinate per tick in monotonically increasing order; resend lookup depends on contiguous tick offsets. When game `kbDesyncDebugFrames` is enabled, also buffer the separate full-frame diagnostic ring. Clear both rings across save-load tick resets and prune coordinates that leave the active set.
- Preserve ring contiguity even when an update payload cannot be encoded; the resulting client CRC mismatch drives resync.
- Revalidate client, slot, epoch, and coordinate immediately before sending a deferred full state because an unsubscribe or disconnect may have invalidated it in the same poll batch.
- Full-state resync and load notification are transport mechanisms; game state reconstruction remains game-owned.

## See Also

- Game Server Session (`../../../../Projects/BrokenEngineSandbox/Source/Network/Server/AGENTS.md`)
- Network architecture (`../../../../Documents/Architecture/Network.md`)
