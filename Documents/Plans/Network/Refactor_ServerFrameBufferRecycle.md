# Refactor: Server BufferFullFrame Allocation Recycling

## Context

Source: /external-refactor-clean on `Engine/Source/Network` (recursive). `Server::BufferFullFrame` is the single biggest steady-state allocator load in the directory: called once per tick with every active coord, it constructs a fresh `std::ostringstream` per coord, serializes the full frame, and copies the result into a new `std::string` stored in the ring. Tracker-compliant (suppressed + `// Heap:`) but two large allocations plus a full copy per coord per tick, forever.

## Design

### Engine/Source/Network/Server/Server.cpp
- `BufferFullFrame` (`Server.cpp:282-291`): the stored payload must outlive the tick (ring buffer), so workbuffer is the wrong tool; buffer reuse is the right one. Options (grill): [~45m]
  - (a) Recycle popped ring entries — when the ring evicts a `BufferedFullFrame` (`:288-291`), move its `std::string` (and any map storage) back into a scratch slot reused by the next `BufferFullFrame` call, so steady-state runs allocation-free once buffers reach high-water size;
  - (b) Serialize through a reusable `std::string`-backed `streambuf` (custom `ostream` over a member buffer), then move/assign into the ring entry — removes the `frameStream.str()` copy as well.
- Keep the single-writer assumption documented in `Server/CLAUDE.md:9` (one shared compression scratch, `Server.h:209`) — the recycled scratch joins that same main-thread-only contract.

## Critical files

- `Engine/Source/Network/Server/Server.cpp` (`BufferFullFrame`), `Server.h` (`PerCoordBufferedFrame` ring, scratch members)
- Read-only: `Projects/BrokenEngineSandbox/Source/Network/Server/ServerBroadcaster.cpp` (per-tick caller)

## Out of scope

- The wire format and LZ4 compression of buffered frames — bytes unchanged.
- Resend semantics (`SendResends` reads the buffered payloads — read side untouched).
- Client-side allocation work (`Refactor_CoordUpdateAllocAndJitter.md`).
- The iostream-based `Frame` serialization API itself — adapters wrap it; no signature changes in game serialization code.

## Acceptance criteria

- Steady-state ticks perform no new heap allocation in `BufferFullFrame` once ring buffers reach high-water size; resends still serve byte-identical payloads (interop + resync playtest).

## Notes

- **Invariant exposure**: server per-tick path feeding resends — payload bytes must remain identical (Risks 2: runtime path, fallback is trivial revert). No CRC/determinism exposure (serialization content unchanged; only buffer lifetime management). One grill decision: recycle shape (a) vs (b).

## Verification Notes

Verified against source (2026-06-10); citations refreshed:
- `BufferFullFrame` spans `Server.cpp:271-292`; the per-coord `std::ostringstream` + `frameStream.str()` copy is `:281-284` (`insert_or_assign` into `serializedFrames`); ring eviction site (`pop_front` past `kiMaxBufferedFrames`) is `:287-291` — option (a)'s recycle hook.
- Call rate confirmed: one call per tick from `ServerBroadcaster::BroadcastStatusChanges` (`ServerBroadcaster.cpp:150-160`) carrying every active coord — context corrected from "called every tick for every active coord" to "called once per tick, per-coord serialization inside"; allocator-load magnitude unchanged. Note this buffering is unconditional (not gated by `kbReplayFullFrames`), and the ring only serves reliable debug-frame requests (`Server::ClientDebugFrameRequest`, compressed lazily at request time via the shared scratch).
- Single-writer contract confirmed: `Server/CLAUDE.md` states the one shared compression scratch is reused across all frames/clients on the server main thread; scratch member `mCompressionBuffer` at `Server.h:209`, full-frame ring `mBufferedFullFrames` at `:206`. A recycled string/streambuf scratch joins the same main-thread-only contract cleanly.
- One precision for resend wording: `SendResends` reads the *per-coord compressed delta* ring, not this full-frame ring — only `ClientDebugFrameRequest` reads `mBufferedFullFrames`. The "resends still serve byte-identical payloads" acceptance criterion is satisfied trivially (untouched); the byte-identical requirement that matters here is the debug-frame payload.
- Adjacent plan, no overlap: `Network/BroadcastTickAllocationGuardScope.md` edits the caller's suppression guard; this plan only changes buffer lifetime inside `BufferFullFrame`. Co-scheduling optional.
