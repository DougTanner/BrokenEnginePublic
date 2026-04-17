# `/Engine/Source/Network/Server/` - Server Networking

## Overview

Server-side ENet host and engine-generic session base. Game sessions inherit `ServerSessionBase`. Server-only (`BT_SERVER`).

## Key Classes

- **Server** (`gpServer`) - ENet host managing client connections, slot-based coord subscriptions, per-coord LZ4-compressed delta ring for resend, and full-frame ring for debug requests. GUID generation via `UuidCreate` (Rpcrt4).
- **ServerSessionBase** - Fixed-rate tick timing via Windows waitable timer. Tick wait is hybrid: sleep until ~2ms before target, then spin to precision. Owns `NetworkDiscoveryResponder`.

## Invariants

- **Drain-per-poll**: `Poll()` clears all pending vectors at entry; game layer must drain them the same tick or data is lost.
- **Handshake gate**: receive handlers reject packets from clients pre-`ClientHello`.
- **Subscribe adjacency**: 3x3 of any authorized coord; `kOriginCoord` always allowed (initial fleet spawn). Authorized list maintained by game layer.
- **Slot reuse**: `FreeSlot` preserves epoch; allocation increments it so stale in-flight packets are discarded by epoch mismatch.
- **ACK floor** clamped to latest buffered tick (future ACKs rejected); 3 consecutive zero-advance ACKs mark slot floor-stalled.
- **Ring pruning**: per-coord rings drop immediately when coord leaves the update set (simulation-boundary contraction).
- **New-subscription race guard**: slot/coord revalidated before sending static + full-state.
- **ENet tuning**: peer throttle disabled so unreliable channels don't drop during reconciliation stalls; 1MB socket buffers.

## See Also

- Parent: [../CLAUDE.md](../CLAUDE.md)
- Game session: [Projects/BrokenEngineSandbox/Source/Network/Server/CLAUDE.md](../../../../Projects/BrokenEngineSandbox/Source/Network/Server/CLAUDE.md)
- [Network.md](../../../../Documents/Architecture/Network.md)
