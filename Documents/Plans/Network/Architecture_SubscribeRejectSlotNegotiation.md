# Architecture: Subscribe-Reject Sentinel and Slot-Count Negotiation

## Context

Source: /external-architecture-review on `Engine/Source/Network` (recursive). The server signals subscription rejection with sentinel slot `0xFF`, but the client detects rejection as "slot index out of my range" — and the two ranges are not negotiated (server allocates from 64 slots per client, client runs 16). A legitimate server accept on slot ≥ 16 is misread as a rejection, and that path — unlike every other ghost flow — sends no unsubscribe, leaking the server slot permanently.

## Design

### Engine/Source/Network/Client/ClientReceive.cpp
- `Client::ServerSubscribeAccept` detects rejection via `uiSlotIndex >= std::ssize(mCoordSlots)` (`ClientReceive.cpp:456`) and returns before the `mCancelledSubscriptions` check (`:456-462`). Replace the range test with an explicit `uiSlotIndex == kuiSubscribeRejectSlot` (`0xFF`) check against a named constant in `NetworkProtocol.h` (today the sentinel exists only as magic literals at `ServerReceive.cpp:354,362` and prose in `Server/CLAUDE.md:20`). [~15m]
- For a genuinely out-of-range *accept* (defense if negotiation is not adopted): send an unsubscribe for the ghost slot instead of silently dropping, so the server-side subscription cannot leak. Today the leaked slot broadcasts updates every tick on a channel the client ignores (`ServerCoordFullState` for slot ≥ 16 silently dropped at `ClientReceive.cpp:166-169`; resends suppressed only because the ACK floor stays −1, `ServerSend.cpp:174`). [~15m]

### Engine/Source/Network/NetworkProtocol.h
- Add `kuiSubscribeRejectSlot = 0xFF` shared by both sides; replace the magic literals at `ServerReceive.cpp:354,362`. [~5m]

### Slot-count negotiation (grill decision)
- The hello packet (`ClientSend.cpp:190-196`) never communicates the client's slot count; the server allocates from `NetworkManager::kiMaxEnetCoordSlots = 64` per client (`Server.cpp:135-136`) while the client runs `kiDesiredCoordSlots = 16` (game `Game.h:33` → `Client.cpp:19-20`). Churn (cancel-while-`kSubscribing` + re-subscribe — the exact ghost flows `ClassifySubscribeAccept` exists for) can push a server allocation to slot ≥ 16. Options: [~30m]
  - (a) Carry the client slot count in `ClientHello` (1 byte; protocol version bump via `kuiProtocolVersion`), server clamps allocation to it and rejects with `0xFF` beyond;
  - (b) No wire change: server clamps to a shared compile-time constant equal to the client's count (couples the two builds — works only while both ship from one repo);
  - (c) Sentinel fix + defensive unsubscribe only (smallest diff; leak closed, slots ≥ 16 still misallocated then immediately unsubscribed).

## Critical files

- `Engine/Source/Network/Client/ClientReceive.cpp` (`ServerSubscribeAccept` / `ClassifySubscribeAccept` commit path)
- `Engine/Source/Network/Server/ServerReceive.cpp` (`kClientSubscribe` handler, `0xFF` literals)
- `Engine/Source/Network/NetworkProtocol.h` (new sentinel constant; `kuiProtocolVersion` if (a))
- `Engine/Source/Network/Client/ClientSend.cpp` (`SendHello` if (a))
- `Engine/Source/Network/Server/Server.cpp` (slot allocation if (a)/(b))

## Out of scope

- The paused-server pending-queue drop (`Architecture_PausedServerSubscriptionQueueDrop.md`).
- The epoch check on `kSubscribing` slots (`Network/StaticDataEpochCheckOnSubscribingSlot.md`, existing plan).
- Raising the client's `kiDesiredCoordSlots` or changing `kiMaxEnetCoordSlots`.
- Any change to the accept/ghost classification flags themselves (`Client.h:149-174` `Classify*` — working as designed).

## Acceptance criteria

- Rejection is detected by sentinel equality, not range; a server accept on any slot the client cannot host results in an unsubscribe reaching the server (no permanently-active orphan subscription).
- If (a): protocol version bumped; old/new mismatch rejects cleanly at handshake.

## Notes

- **Invariant exposure**: network protocol. Option (a) changes the `ClientHello` wire format (gated by `kuiProtocolVersion` reject — clean). Options (b)/(c) are receive-logic only. No CRC/replay exposure (subscription control plane, not sim state).
- Grill decision: negotiation shape (a)/(b)/(c). (a) is the structurally honest fix; (c) is the minimum that closes the leak.

## Verification Notes

All items verified against source (2026-06-10):
- Sentinel literals exact: `SendSubscribeAccept(*pClient, 0xFF, coord)` at `ServerReceive.cpp:354` (not-adjacent) and `:362` (no free slot). Client rejection detection via `uiSlotIndex >= std::ssize(mCoordSlots)` at `ClientReceive.cpp:456`; that branch only clears the placeholder and logs — no unsubscribe sent, confirmed.
- Leak chain confirmed: full state for slot ≥ 16 dropped at `ClientReceive.cpp:166-169` *before* `ClassifyFullState`'s ghost-unsubscribe handling; static data likewise dropped (`:238-241`) and updates (`:311-314`). Resends suppressed only because the never-ACKed slot's floor stays −1 (`ServerSend.cpp:174` `continue`).
- Un-negotiated counts confirmed: server `kiMaxEnetCoordSlots = 64` per client (`NetworkManager.h:19`, allocated `Server.cpp:135-138`); client `kiDesiredCoordSlots = 16` (`Game.h:33` → `ClientSession.cpp:234` → `Client.cpp:19-20`); hello payload (`ClientSend.cpp:190-196`) carries protocol version, frame version, build config, GUID — no slot count.
- Impact slightly stronger than written: a leaked `bActive` subscription also keeps its coord in the server's active set every tick via `ServerSession::AddSubscribedCoords` (`ServerSession.cpp:264-279`) — perpetual sim cost, not just wasted sends. And a client re-subscribe to the same coord gets silently swallowed (`ServerReceive.cpp:332-336` AlreadySubscribed returns without replying), so the client's new `kSubscribing` placeholder wedges too and its eventual cancellation strands an entry in `mCancelledSubscriptions`.
- Reaching slot ≥ 16 requires >16 concurrently-active server-side subscriptions for one client — only achievable through cancel-while-`kSubscribing` churn inside an RTT window (each ghost is unsubscribed only when its accept arrives). Edge-case frequency, permanent consequence — Impact as scored holds.
