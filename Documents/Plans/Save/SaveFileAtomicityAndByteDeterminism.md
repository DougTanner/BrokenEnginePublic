# Save-File Atomicity Commit Fix + Byte-Determinism Cleanup

## Context

2026-07-03 review sweep of the save/replay write path. One real data-loss bug in the atomic-write commit, plus a cluster of byte-determinism warts that defeat file-level diffing of saves/replays (a stated goal — `WriteGrid` sorts coord keys "for deterministic output"; replay doubles as the determinism harness, so byte-diffable recordings matter).

1. **`WriteFileAtomically` samples `good()` before `close()`** — `FileManager.h:278-282` (verified this session): `fnWrite(stream); bool bGood = stream.good(); stream.close();`. The fstream's buffered tail flushes in `close()`; if that flush fails (disk full, I/O error), `failbit` is set *after* `bGood` was sampled true, and `CommitAtomicWrite` renames the truncated `.tmp` **over the previous good file** — precisely the loss the temp-then-rename scheme exists to prevent.
2. **Replay save is three separately-atomic files, not an atomic set** — `DifferenceStream.h` `DifferenceStreamWriter::Save`: header, `.frames`, `.checksums` via three `WriteFileAtomically` calls with results discarded (`static_cast<void>`). A failure/crash between commits pairs a new header with stale sibling files; stale checksums then emit spurious CRC-mismatch reports — poisoning the harness signal.
3. **`F7.replay.manifest` iterates an `unordered_map`** — `GameSaveLoad.cpp:302-311` writes `mReplayWriters` in hash order: manifest bytes (and `recordedCoords.front()`, which seeds playback tick/time) vary per recording run. (The analogous grid-save fleet-block ordering — `WriteFleetData` hash-order iteration — is **already owned by `Network/AuditSweepQuickWins.md`** ("deterministic fleet-block save order"); not duplicated here.)
4. **`F7.replay.manifest` has no version header** — raw `[count][coords…]`; every sibling file is version-gated.
5. **`ReplayMeta` writes 4 bytes of tail padding** — `Game.h:40-46` `{GridCoord(8), int64_t(8), float(4)}` → `sizeof == 24`, raw-byte `WriteVersionedFile` path; designated-init doesn't guarantee zeroed padding, violating `Serialization.h`'s "padding-free or zeroed" contract and byte-determinism.

## Design

1. In `WriteFileAtomically`: `stream.close(); const bool bGood = !stream.fail();` (close, then test — `close()` folds the final flush result into the state), then commit on that.
2. In `DifferenceStreamWriter::Save`: propagate the three `WriteFileAtomically` results; on any failure, delete the partial set (all three paths + `.tmp` leftovers are already handled by the atomic helper) and log `kError` once with which file failed. Keep three files (single-file merge is a bigger format change than the harness needs — YAGNI); the header-written-last ordering plus set-delete-on-failure closes the observed torn-set window for process-level failures.
3. In the manifest writer: copy the writer coords into workbuffer scratch, sort by `GridCoord::ToKey()` (the `WriteGrid` pattern), write sorted.
4. Wrap the manifest in `WriteVersionHeader`/`ReadAndValidateVersionHeader` keyed on a small local version constant (breaks old dev-only manifests — acceptable; replays are transient dev artifacts, and the reader already rejects garbage counts).
5. Add explicit trailing `uint8_t uiPad[4] {};` to `ReplayMeta` (the `DataFile.h` `SceneHeader` pattern). `sizeof` unchanged (24) → no `kiVersion` bump needed; old metas read identically.

## Critical files

- `Engine/Source/File/FileManager.h` — `WriteFileAtomically`
- `Engine/Source/File/DifferenceStream.h` — `DifferenceStreamWriter::Save`
- `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp` — manifest write/read (`SaveLoadReplay`)
- `Projects/BrokenEngineSandbox/Source/Game.h` — `ReplayMeta`

## Invariant exposure

- **No sim/CRC/wire exposure.** Item 1 affects every atomic write (saves, replays, versioned settings) — failure-path only; the success path is byte-identical. Items 3-5 change replay-file bytes (manifest order/header, meta padding): replay *files* are dev artifacts gated by `Frame::kiVersion`/`ReplayMeta::kiVersion`; reconstructed state is unchanged (readers are order-independent / padding-blind). Item 4 invalidates pre-change manifests by design.

## Out of scope

- `FlushFileBuffers`-to-media before rename (power-loss durability) — review finding, but process-crash safety is the scheme's stated goal and the version header already detects a torn post-power-loss file; not worth an fsync on every autosave. Documented here as the deliberate accept.
- Fleet-block save ordering — owned by `Network/AuditSweepQuickWins.md` (see Context).
- Single-file replay container or cross-file generation IDs.
- `WriteVersionedFile` const-ness — owned by `Network/Refactor_NetworkFileResidual.md`.

## Acceptance criteria

- Simulated write failure (e.g. temporarily forcing the stream bad before close) leaves the previous destination file intact and logs the failure.
- Two identical recording runs produce byte-identical `F7.replay.manifest` and `.meta` files.
- Existing grid saves load unchanged; old replay manifests are rejected with a version log line rather than misparsed.

## Notes

- Grill decision pre-staged: none — item 2's keep-three-files shape is the one judgment call and is resolved above (KISS); revisit only if the grill wants the single-file container.
- Sequencing: `FileManager.h` is in the FileManager File Group (co-scheduled plans there — refresh line cites if interleaved). `GameSaveLoad.cpp` is shared with `Save/SaveLoadTrustBoundaryHardening.md` — co-schedule the two Save plans in one session.
