# Gate the Desync-Debug Infrastructure (Full-Frame Buffering + Client-Triggerable Debug Paths)

## Context

The server's desync-debug machinery runs unconditionally in production and is client-triggerable with no throttle. Two problems:

### (a) Unconditional full-frame buffering

`Server::BufferFullFrame` (`Engine/Source/Network/Server/Server.cpp:265`) is called every tick from `ServerBroadcaster::BroadcastTick` (`Projects/BrokenEngineSandbox/Source/Network/Server/ServerBroadcaster.cpp:159`) inside an ungated block (`:150-160`). Every active coord's **full** `game::Frame` is stream-serialized (`mFrameStream << *rFrame.second`, `Server.cpp:294-295`) into a 256-deep ring (`kiMaxBufferedFrames`, `NetworkProtocol.h:69`) whose only consumer is `kClientDebugFrameRequest` (`ClientDebugFrameRequest`, `ServerReceive.cpp:162`). This costs per-tick full-frame serialization CPU plus up to `256 × activeCoords × frame-size` resident memory in every production run. The string-pool recycling (`mFullFramePool`, `Server.cpp:272-292`) already avoids per-tick allocation, but the *feature* is unconditional. Note the 256 depth is the delta-ring's resend requirement — a desyncing client requests recent ticks, so this ring does not need the same depth.

### (b) Unthrottled client-triggerable debug paths

Any handshaken client can spam two packets with no rate limit:
- `kClientDesyncReport` → `Server::ClientDesyncReport` (`ServerReceive.cpp:136`): one `kError` log per packet (`:159`), gated only on `FindHandshakenClient`.
- `kClientDebugFrameRequest` → `Server::ClientDebugFrameRequest` (`ServerReceive.cpp:162`): `kError` log (`:181`) + full-frame ring lookup + LZ4 compress + a large reliable send, all per 17-byte request.

Both amplify a tiny request into CPU/bandwidth, and both flood the in-memory log ring that feeds crash reports (`kError` is always logged).

## Design

- **(a)** Gate full-frame buffering. Options (pre-staged for grill — pick one):
  1. **Compile-time flag** — an `inline constexpr bool` (game `Pch.h` alongside `keNetworkSimulation`, or `NetworkProtocol.h`) wrapping the `ServerBroadcaster.cpp:150-160` block and the serve path; zero production cost, no runtime debug capability.
  2. **On-demand** — start buffering only after the first `kClientDesyncReport` / `kClientDebugFrameRequest` arrives (a server-side `mbDebugCaptureArmed` flag); first request misses (acceptable — a desyncing client keeps reporting).
  3. **Shrink the ring** — a dedicated small depth constant for the full-frame ring (decoupled from `kiMaxBufferedFrames`), since debug requests target recent ticks only.
- **(b)** Add a per-client cooldown counter (tick-based) on both `ClientDesyncReport` and `ClientDebugFrameRequest` in `ClientConnection` (`ServerTypes.h`), resetting like the existing `resendLogCooldowns` pattern; drop over-rate requests silently (or `kVerbose`). Optionally gate the debug-frame *serving* behind the same server-side flag chosen in (a). Cooldown fields must be sized/reset wherever `ClientConnection` slot vectors are initialized (`Server::Connect`, `Server.cpp:126-132`).

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerBroadcaster.cpp` — the `BroadcastTick` full-frame-buffering block (gate the `BufferFullFrame` call).
- `Engine/Source/Network/Server/Server.cpp` — `BufferFullFrame`, `Server::Connect` (cooldown-field init if per-client counters added).
- `Engine/Source/Network/Server/ServerReceive.cpp` — `ClientDesyncReport`, `ClientDebugFrameRequest` (cooldowns + optional serve gate).
- `Engine/Source/Network/Server/ServerTypes.h` — `ClientConnection` (new cooldown counters).
- `Engine/Source/Network/NetworkProtocol.h` and/or game `Pch.h` — new gating flag / ring-depth constant (per grill choice).

## Acceptance criteria

- With the debug feature disabled/unarmed, `BufferFullFrame` does no per-tick frame serialization and holds no full-frame ring memory.
- A client flooding `kClientDesyncReport` or `kClientDebugFrameRequest` is rate-limited: bounded `kError` log volume and bounded compress/send work per client per interval.
- Legitimate desync debugging still functions when enabled/armed.

## Out of scope

- The per-coord LZ4 delta ring (`BufferFrame` / `mPerCoordBufferedFrames`) — that is the live resend path, not debug infra; untouched.
- The client-side desync-debug freeze-floor behavior (`ClientSessionBase`).
- Reworking the debug-frame wire format or the crash-report log ring itself.

## Notes

- **Invariant exposure**: none — server-side debug infrastructure only; no wire/CRC/`kiVersion`/determinism change. All edits are `BT_SERVER` paths. Allocation-tracked: gating removes per-tick work rather than adding any; cooldown counters are POD in `ClientConnection`.
- **Grill decision**: the (a) gating strategy (compile-flag vs on-demand-arm vs shrink-ring). Recommend compile-time flag for zero production cost, with (b) cooldowns applied unconditionally so the log/amplification surface is bounded even when a build ships with the flag on.
