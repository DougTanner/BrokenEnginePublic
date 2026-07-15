# Reject Mismatched Pack Data at Connect Instead of Silently Desyncing

## Context

`FileManager::LoadChunk` (`Engine/Source/File/FileManager.cpp:596-620`) deliberately soft-fails corrupt/truncated chunks to zero-filled `kReady` data (logged + `DEBUG_BREAK`). For island chunks the *simulation* consumes (heightmap → `BuildElevationGrid`, nav contour), a machine with a damaged or mismatched pack silently simulates on zeroed elevation/nav data while peers use real data — a guaranteed desync that nothing attributes to the pack. The hello handshake validates protocol and frame versions, but nothing about pack content; pack bytes are otherwise deterministic across machines (the landed `Architecture_PackByteDeterminism` work), so byte-level comparison is meaningful.

## Design

Add a pack-integrity token to `kClientHello` and its accept path: the server compares the client's token against its own and rejects with a distinct reason on mismatch.

Decision (2026-07-03): manifest-derived token — CRC over the manifest's chunk table (per-chunk crc + size + offset — already in memory at boot). Cheap; catches mismatched pack builds and layout/content drift (the realistic case); does not catch on-disk bit rot inside an individual chunk payload. (Whole-pack hash at boot rejected: lazy chunks are not eagerly read, so hashing would force a full pack read at boot — too expensive. Escalating the runtime soft-fail into a session-terminating error on later bit rot is out of scope — this plan is connect-time token only.)

Wire change: extend the hello payload and bump `NetworkProtocol.h`'s `kiVersion` — mismatched builds already can't connect, so no compatibility concern.

## Critical files

- `Engine/Source/Network/` hello send/receive sites (`Client.cpp` / `Server.cpp` / `NetworkProtocol.h`)
- `Projects/BrokenEngineSandbox/Source/Network/ClientSession.cpp`, `ServerSession.cpp` — reject-reason surfacing
- `Engine/Source/File/FileManager.{h,cpp}` — token computation over the manifest chunk table

## Out of scope

- Changing the `LoadChunk` soft-fail policy for non-sim (texture) chunks — client-visual soft-fail stays (see `Graphics/Managers/CorruptTextureChunkLifecycleHardening.md` for its GPU-side hardening).
- The send/receive pairing restructure (`Network/Architecture_WireFormatPairing.md`) — the new hello field is written/read hand-mirrored like its neighbors for now.

## Coordination

- Protocol/version batch with `Documents/Plans/Network/SubscriptionLifecycleRaceHardening.md`, `Documents/Plans/Network/FleetRequestsByGuid.md`, `Documents/Plans/Network/WireFormatPairingGameSide.md`: land the wire breaks in one client/server release behind one `kuiProtocolVersion` bump; the last lander owns the consolidated bump.
- `Documents/Plans/Network/Architecture_WireFormatPairing.md`: never interleave send/receive-site restructuring with this wire change. WireFormatPairingGameSide's structured dependency requires the architecture plan first.

## Notes

- Invariant exposure: **wire change** (hello layout + protocol version bump). No determinism/CRC-of-frame exposure — this *prevents* an un-diagnosable desync class.
- Coordinate with the live `Network/Architecture_WireFormatPairing.md` (both touch the hello/message sites): land either first and refresh the other's citations; do not interleave.
- Decision (2026-07-03): manifest-derived token (Option B); see Design.
