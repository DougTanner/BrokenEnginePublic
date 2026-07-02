# Bump Frame::kiVersion for the Landed CRC-Mixing Algorithm Change

## Context

Commit c1b403d3 (executed plan `Architecture_CollectionCrcMixing`) changed frame CRC mixing from an XOR fold to the ordered `(checksum ^ crc) * common::kCrcMultiplier` fold across all mixing sites (`MultiCrc`, `Collection::Crc`, `CollectionCrc`, both bases in `FrameBase.cpp`, game `Frame::Crcs`). Every computed frame CRC changed, but the `Frame::kiVersion` base constant (the `115` in `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp:11`) was not bumped — the only version delta in the surrounding range (the `engine::ExplosionsInterpolate::kiVersion` term) landed in the earlier commit 90ad1706, before the CRC change.

Consequences:
- Replay `.checksums` sidecars (`DifferenceStreamWriter::mChecksums`, gated only by `ReadAndValidateVersionHeader<SAVED_TYPE>`) recorded by a build between 90ad1706 and c1b403d3 pass the version gate but fail checksum comparison → false desync reports.
- A client/server pair straddling c1b403d3 passes the version handshake yet permanently CRC-mismatches.

## Design

1. Bump the base constant 115 → 116 in the `Frame::kiVersion` sum.
2. Add a comment at `common::kCrcMultiplier` (`Common/Crc.h`) and at the `Frame::kiVersion` definition: **any change to the CRC mixing algorithm must bump `Frame::kiVersion`** — the version gate is the only thing distinguishing "data desynced" from "checksum algorithm changed".
3. Optionally document at `FrameInput::Crc` (`Projects/BrokenEngineSandbox/Source/Input/Input.cpp:140`) why *it* needs no bump: that CRC is same-session-only (DifferenceStream writer-side dedup + a verbose reader log), never persisted or compared cross-build.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp:11` — the `Frame::kiVersion` definition
- `Common/Crc.h` — `kCrcMultiplier` (comment only)
- `Projects/BrokenEngineSandbox/Source/Input/Input.cpp` — `FrameInput::Crc` (comment only, optional)

## Out of scope

- Any change to the CRC algorithm itself or to any mixing site.
- Mechanically enforcing the bump rule (e.g. hashing the algorithm) — a comment at the two sites is proportionate.

## Notes

- Invariant exposure: version gate for saves/replays/network handshake. Bumping deliberately invalidates pre-bump saves and replays (dev-only cost; replays recorded in the broken window are already unusable — they false-desync).
- Grill decision (single): plain bump (recommended — KISS, one shared gate) vs a dedicated `kiCrcAlgorithmVersion` constant folded into the sum so future CRC-algorithm bumps are self-documenting. Old grid saves' *data* is unaffected by the CRC algorithm, so a split constant would let saves survive CRC-only bumps — but a split gate is extra machinery for a rare event.
