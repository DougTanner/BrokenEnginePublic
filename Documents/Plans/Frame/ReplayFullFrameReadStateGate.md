<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-03T00:59:11.378Z","dependsOn":[]} -->
# Gate Replay Full-Frame Diagnostics on Stream State

## Context

Found during Tier-3 preparation and adversarial review of the active `Documents/Plans/Frame/ExplosionTrailCountReadClamp.md` change (accepted residual PA-F-001). The active change owns only normalization of deserialized explosion trail counts. Its approved boundary explicitly excludes the separate truncated replay follow-up, and the session does not modify `Engine/Source/File/DifferenceStream.h`.

The optional replay `.fullframes` stream is read without checking whether each snapshot was completely deserialized. In `engine::DifferenceStreamReader`'s constructor, `mFullFramesStream >> firstFrame` (`Engine/Source/File/DifferenceStream.h:380-382`) is followed immediately by `firstFrame.Crc()`. On later ticks, `ValidateChecksum` extracts `savedFrame` (`DifferenceStream.h:445-448`), unconditionally marks it valid (`:449`), and can call `savedFrame.LogDifferences(rSavedCurrent)` (`:459-464`) on a checksum mismatch. A truncated snapshot therefore leaves a partially populated frame reachable from CRC or difference traversal. `Collection::Read` and `game::operator>>` can also throw `common::CorruptStreamException` before an extraction returns, so checking stream state alone cannot cover every failed snapshot.

The frame extractor itself does not report success separately: `game::operator>>` reads into a temporary and move-assigns it to the destination even when the stream has failed (`Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp:758-770`), while `common::Read` only calls `istream::read` and relies on the stream state (`Common/Serialization.h:87-92`). The writer emits one full-frame snapshot for each checksum index, plus the initial baseline when it records the initial checksum (`Engine/Source/File/DifferenceStream.h:35-63`), so an open sibling remains active even when zero bytes remain at an expected boundary; `in_avail() > 0` would silently skip that truncation. The existing persisted-grid boundary checks the stream after reading and refuses adoption (`Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp:1253-1256`); the reader's optional diagnostic stream has no equivalent gate. This is pre-existing and outside the approved explosion-count implementation boundary, not an acceptance failure of that active Plan.

## Design

Treat `.fullframes` as optional diagnostics. Track sibling presence separately from failure with `ReaderFlags` bits for an active and an invalid full-frame stream: an absent sibling leaves both clear, opening it sets the active bit, and any failed or stale read sets the invalid bit while gating further reads. This preserves one-way disable without retrying or mistaking an active zero-byte stream for absence.

1. In the constructor, after the `.fullframes` sibling opens, mark it active. If the reader records the initial checksum, always attempt the initial baseline extraction, even when no bytes are available. Wrap that extraction in `catch (const common::CorruptStreamException&)` and inspect the stream's boolean state (failure/bad state, not `eof()`) before calling `Crc()`. Route either failure through one bounded `kWarning`/disable path that marks the stream invalid and discards optional diagnostic state; keep the core replay/checksum files loaded so playback continues without detailed frame comparisons. A stale baseline mismatch uses that same disabled state while retaining its existing warning and `DEBUG_BREAK()`.
2. In `ValidateChecksum`, when the sibling is active and not invalid and `iChecksumIndex == miFullFramesIndex`, attempt one snapshot extraction for every expected checksum index, including when `in_avail() == 0`. Catch `common::CorruptStreamException` around the extraction and inspect stream state before accepting it. Set `bSavedFrameValid` and advance `miFullFramesIndex` only after success; route either failure through the same one-warning/disable path and skip `LogDifferences`. The normal checksum comparison and difference replay still proceed.
3. Once the invalid flag is set, do not retry reads or traverse the failed frame. An absent sibling remains inactive and is never read; an active sibling is disabled on the first failed or stale read. Do not change the replay's core failure policy.

The policy is intentionally limited to the optional diagnostic sibling: complete replay/checksum streams remain authoritative, while a truncated `.fullframes` file loses only detailed comparison output. No new format or compatibility decision is needed.

## Critical files

- `Engine/Source/File/DifferenceStream.h` — `DifferenceStreamReader` constructor full-frame baseline read (`:371-395`), `ValidateChecksum` full-frame extraction and diagnostic call (`:425-470`), and `ReaderFlags`/full-frame state (`:510-533`); the only edited implementation file.
- `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp` — `game::operator>>` (`:758-770`), read-only evidence that a failed stream can still move a partial frame into the destination.
- `Common/Serialization.h` — `common::Read` (`:87-92`), read-only evidence of failbit-only short reads.
- `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp` — `ReadGrid` post-read stream gate (`:1253-1256`), read-only precedent.

## In scope

- `DifferenceStreamReader`'s optional `.fullframes` handling in `Engine/Source/File/DifferenceStream.h`: distinguish an absent sibling from an active one, catch `common::CorruptStreamException`, and inspect stream state after the constructor's initial baseline extraction and after each `ValidateChecksum` extraction before any frame `Crc()` or `LogDifferences` call.
- Add and use reader-state bits for active and invalid full-frame diagnostics; an active sibling must attempt every expected checksum-index snapshot even with zero bytes remaining, while a failed or stale read disables further reads. Avoid setting `bSavedFrameValid` or incrementing `miFullFramesIndex` for a failed read.
- Route failbit, badbit, and `CorruptStreamException` through one bounded `kWarning`/disable path and preserve normal checksum logging and replay advancement.

## Out of scope

- `DifferenceStreamWriter`, `.fullframes` writing, replay file naming, stream formats, `Frame::kiVersion`, collection layout, serialization order, wire protocol, or CRC composition.
- Changes to `common::Read`, generic frame deserialization, save/load, network receive, or the active explosion trail-count clamp; those are separate boundaries and plans.
- Rejecting an otherwise valid replay because its optional diagnostics are truncated, retrying or repairing the file, or changing broader replay error policy.
- Per-collection/per-consumer validation, new compatibility paths, and unit tests.

## Risk tier and invariants

Tier 3 — trust-boundary handling of persisted replay input on a deterministic CRC/checksum path, with optional diagnostic state shared by client/server builds.

- Complete `.fullframes` snapshots and known-good replay/checksum files retain today’s CRC and `LogDifferences` behavior byte-for-byte.
- A failed initial or later optional snapshot read, whether it sets stream failure or throws `common::CorruptStreamException`, cannot reach that frame's `Crc()` or `LogDifferences`, cannot be retried on later ticks, and cannot throw out of the constructor's optional extraction or `ValidateChecksum`; core checksum comparison and replay progression remain unchanged.
- An opened `.fullframes` sibling owes one snapshot for every expected checksum index, so an attempt at zero remaining bytes fails and disables diagnostics; an absent sibling is not read, and a stale-baseline discard enters the same disabled state.
- No replay, save, network, `.pack`, wire, layout, or version bytes change. The read-loop addition must not introduce a heap allocation on the tick path.

## Acceptance criteria

- A replay whose initial `.fullframes` snapshot is truncated logs one warning, disables full-frame diagnostics before `firstFrame.Crc()`, and still loads/plays when the core replay and checksum siblings are valid.
- A replay whose initial or later `.fullframes` snapshot truncation causes `common::CorruptStreamException` during extraction catches it at the extraction site, routes it through the same one-warning/disable policy, and lets neither the constructor nor `ValidateChecksum` throw from that optional read.
- A replay whose later active `.fullframes` snapshot is truncated logs one warning, never calls `LogDifferences` for that partial frame, does not retry on subsequent ticks, and continues normal checksum validation and difference playback.
- A present/active `.fullframes` sibling whose next expected snapshot has zero bytes remaining is still attempted and then disables diagnostics on failure; a missing sibling remains inactive and unaffected.
- A stale initial baseline enters the same disabled state, so later checksum indices do not retry or traverse that sibling.
- A complete `.fullframes` replay, including checksum mismatches, produces the same baseline CRC and detailed difference traversal as before the change.
- A replay without a `.fullframes` sibling is unaffected, and no exception escapes `ValidateChecksum` for a truncated optional diagnostic stream.
- Debug client and server builds pass; no unit tests are added.

## Notes

- Line citations are from the current tree on 2026-08-02; refresh them if `DifferenceStreamReader` or frame extraction moves before implementation.
