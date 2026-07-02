# Reject Mismatched Pack Data at Connect Instead of Silently Desyncing

## Context

`FileManager::LoadChunk` (`Engine/Source/File/FileManager.cpp:596-620`) deliberately soft-fails corrupt/truncated chunks to zero-filled `kReady` data (logged + `DEBUG_BREAK`). For island chunks the *simulation* consumes (heightmap → `BuildElevationGrid`, nav contour), a machine with a damaged or mismatched pack silently simulates on zeroed elevation/nav data while peers use real data — a guaranteed desync that nothing attributes to the pack. The hello handshake validates protocol and frame versions, but nothing about pack content; pack bytes are otherwise deterministic across machines (the landed `Architecture_PackByteDeterminism` work), so byte-level comparison is meaningful.

## Design

Add a pack-integrity token to `kClientHello` and its accept path: the server compares the client's token against its own and rejects with a distinct reason on mismatch. Token options (grill):

- **A — whole-pack hash at boot.** Strongest, but lazy chunks are *not* eagerly read, so hashing forces a full pack read at boot — likely too expensive.
- **B — manifest-derived token (recommended).** CRC over the manifest's chunk table (per-chunk crc + size + offset — already in memory at boot). Cheap; catches mismatched pack builds and layout/content drift (the realistic case); does not catch on-disk bit rot inside an individual chunk payload.
- **C — B plus escalating the runtime soft-fail.** Keep B for connect-time, and additionally convert the island-chunk `LoadChunk` soft-fail into a session-terminating error (disconnect with reason) when it fires while connected. Catches bit rot too; more machinery.

Wire change: extend the hello payload and bump `NetworkProtocol.h`'s `kiVersion` — mismatched builds already can't connect, so no compatibility concern.

## Critical files

- `Engine/Source/Network/` hello send/receive sites (`Client.cpp` / `Server.cpp` / `NetworkProtocol.h`)
- `Projects/BrokenEngineSandbox/Source/Network/ClientSession.cpp`, `ServerSession.cpp` — reject-reason surfacing
- `Engine/Source/File/FileManager.{h,cpp}` — token computation over the manifest chunk table

## Out of scope

- Changing the `LoadChunk` soft-fail policy for non-sim (texture) chunks — client-visual soft-fail stays (see `Graphics/Managers/CorruptTextureChunkLifecycleHardening.md` for its GPU-side hardening).
- The send/receive pairing restructure (`Network/Architecture_WireFormatPairing.md`) — the new hello field is written/read hand-mirrored like its neighbors for now.

## Notes

- Invariant exposure: **wire change** (hello layout + protocol version bump). No determinism/CRC-of-frame exposure — this *prevents* an un-diagnosable desync class.
- Coordinate with the live `Network/Architecture_WireFormatPairing.md` (both touch the hello/message sites): land either first and refresh the other's citations; do not interleave.
- Grill decision: token option A/B/C above (recommend B).
