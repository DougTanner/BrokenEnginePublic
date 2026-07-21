# Engine Network - Transport and Protocol Infrastructure

Shared ENet transport, slot subscriptions, ACK state, discovery, wire cursors, and hostile-input enforcement. Game packet payloads and reconciliation policy belong to [game Network](../../../Projects/BrokenEngineSandbox/Source/Network/AGENTS.md); flow and timing details live in [Network architecture](../../../Documents/Architecture/Network.md).

## Transport Contracts

- Use `NetworkManager` channel helpers for control and per-coordinate reliable/unreliable channels. All sends go through `SendPacket`; fixed arithmetic or `GridCoord` payloads use `SendSimplePacket`.
- Each coordinate slot has independent ACK floor, bitfield, and epoch state. Epoch mismatch drops stale traffic after slot reuse; unsubscribe carries the observed epoch, so a stale request cannot free a reused slot while the server still ACKs the no-op.
- Engine packet types remain below `kGamePacketStart`; game packets are forwarded opaquely.
- The handshake verifies protocol version, deterministic Frame version, and ordered island-manifest identity before accepting a peer.
- Cursor primitives are unchecked. Validate exact fixed layouts or gate every variable-length read with `BoundedCursor` before passing its cursor to a primitive.
- Client-to-server packet additions require a contract row, size and semantic validation, handshake/debug gating where applicable, per-tick rate limits, and violation reporting through the server contract path. Send commands at tick cadence, not render cadence.
- ENet service, discovery polling, sends, and simulation queues are main-thread-only. Their workbuffer and state have no locking by design.

## Timing and Polling

- Rendering smoothness takes priority over command round-trip latency. Client simulation and presentation intentionally retain buffered committed ticks; tuning should preserve smooth pacing rather than introduce stalls or bursts.
- Both peers drain transient poll outputs each update. New-subscription and resync requests persist until broadcast servicing, including the paused/zero-tick path.
- Network simulation injects deterministic one-way delay and burst loss above ENet. Reliable packets may be delayed but are never deliberately dropped, and each channel preserves FIFO release order.
- Wire serialization is little-endian x64. Network buffer capacity is tick-rate-independent; jitter safety is wall-clock time.

## Ownership

- `NetworkManager` owns ENet lifetime, channel math, and the allocation-suppressed send path.
- `NetworkProtocol` owns shared packet identifiers, compatibility constants, slot identity, and ACK structures.
- Client and server leaves own the side-specific transport peers, receive buffers, contracts, slots, and packet handling.
- `ClientSessionRuntime` and `ServerSessionRuntime` own reusable connection/discovery, subscription/queue, clock/pacing, poll, flush, resend, and reset sequencing. Canonical game sessions compose them and supply synchronous typed policy hooks.
- `game::NetworkSessionContract` supplies Frame/status types, protocol constants, codecs, and game packet contracts at compile time. Runtimes use no virtual session base, runtime type erasure, or additional global manager.
- `NetworkSerialization` declares the game-defined status-change codec without owning its payload format.

## See Also

- [Client](Client/AGENTS.md) - Client transport peer, receive guards, slot state, and metrics
- [Server](Server/AGENTS.md) - Server transport host, contracts, slots, and resend state
- [Game Network](../../../Projects/BrokenEngineSandbox/Source/Network/AGENTS.md) - Concrete session policy and game wire payloads
- [Network architecture](../../../Documents/Architecture/Network.md) - Protocol flow and ACK/reconciliation timing
- [Game reconciliation](../../../Documents/Architecture/GameReconciliation.md) - Rollback and replay integration
