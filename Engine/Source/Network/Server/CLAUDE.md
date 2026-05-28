# `/Engine/Source/Network/Server/` - Server Networking

## Overview

Server-side ENet host and engine-generic session base. Game sessions inherit `ServerSessionBase`. Server-only (`BT_SERVER`).

## Key Classes

- **Server** (`gpServer`) - ENet host owning per-client `ClientConnection` records and two ring buffers: a per-coord LZ4-compressed delta ring for unreliable update/resend, and a full-frame serialized ring serving reliable debug-frame requests. Reuses one persistent compression scratch buffer across frames.
- **ServerSessionBase** - Fixed-rate tick timing via a high-resolution waitable timer. Tick wait is hybrid: timer-sleep until ~2ms before target, then spin to precision (overshoot tracked, logged only under `kbProfilingFrameSpike`). Owns `NetworkDiscoveryResponder`; `PollNetworkBase` services both host and responder.

## Invariants

- **Handshake gate**: handlers other than `ClientHello` reject packets from clients pre-handshake. Hello validates protocol version and `game::Frame::kiVersion` (mismatch rejects + disconnects), warns on build-config mismatch, and mints a `ClientGuid` (`UuidCreate`, Rpcrt4) only when the client sent an empty one.
- **Subscribe adjacency**: 3x3 of any authorized coord; `kOriginCoord` always allowed (initial fleet spawn). Authorized list maintained by game layer. Rejected/no-free-slot subscribes reply with sentinel slot `0xFF`.
- **Slot reuse**: `FreeSlot` preserves epoch; subscribe increments it so stale in-flight packets are discarded by epoch mismatch.
- **ACK floor** clamped to latest buffered tick (future ACKs rejected); client timestamp echo is monotonic-guarded. 3 consecutive zero-advance ACKs mark a slot floor-stalled (peak tracked for resolve logging).
- **Ring pruning**: per-coord delta rings drop immediately when a coord leaves the active update set (simulation-boundary contraction); both rings cap at `kiMaxBufferedFrames`.
- **New-subscription race guard**: slot/coord revalidated before sending static + full-state (`SendNewSubscriptionFullStates`, driven by game layer post-tick).
- **Resend logging is delta-only**: per-slot prev-count + cooldown suppress steady-state spam; only resend-state transitions log.
- **Network simulation**: when enabled, incoming packets route through a delayed-packet queue keyed by release time; fast-forward (`miTimeMultiply > 1`) flushes the queue immediately, and disconnect purges that peer's pending packets.

## See Also

- Parent: [../CLAUDE.md](../CLAUDE.md)
- Game session: [Projects/BrokenEngineSandbox/Source/Network/Server/CLAUDE.md](../../../../Projects/BrokenEngineSandbox/Source/Network/Server/CLAUDE.md)
- [Network.md](../../../../Documents/Architecture/Network.md)
