# Architecture: CRC Mixing Strength (XOR → Ordered Fold)

## Context

Source: /external-architecture-review on `Engine/Source/Frame` (non-recursive); scope completed at verification. The determinism-validation CRC combines sub-hashes with XOR at every level: `Alignments::Crc` XORs per-pair hashes (`Alignments.cpp:80-91`), `FrameInterpolateBase::Crcs()`/`FramePostRenderBase::Crcs()` XOR-combine scalar-field hashes and per-collection CRCs (`FrameBase.cpp:6-20, 70-85`), and the game layer continues the pattern on top: `game::FrameInterpolate::Crcs`/`FramePostRender::Crcs` XOR in game fields and collections (`Projects/.../Frame/Frame.cpp:534-549, 612-627`) and `Frame::Crcs()` XORs the two halves together (`Frame.cpp:692`). XOR is order-independent but symmetric: any even-multiplicity error cancels — e.g., swapping `uiFlags` between two alignment pairs, identical corruption in two collections, or identical corruption in the interpolate and postRender halves, produces an unchanged shared CRC. Desyncs can hide from validation and surface later as harder-to-localize divergence. Order-independence is not actually required at any site: the alignments flat vector is sorted (deterministic iteration), the `Crcs()` folds walk compile-time tuples (fixed order), and each frame's CRC is computed single-threaded on its own dispatch worker (`FrameTick.cpp:74`) — no cross-thread accumulation anywhere.

## Design

### Engine/Source/Frame/Alignments.cpp
- In `Alignments::Crc` (lines 80–91): replace the per-pair XOR accumulation with an ordered fold (e.g., `crc = crc * common::kCrcMultiplier + pairHash`, matching the `common::Crc` byte-fold convention, `Common/Crc.h:50,57-67`) — the sorted flat vector makes iteration order deterministic on both builds [~10m]

### Engine/Source/Frame/FrameBase.cpp
- In both `Crcs()` implementations (lines 6–20, 70–85): replace the XOR combine of scalar hashes and per-collection CRCs with the same ordered fold — tuple order is compile-time fixed and identical across builds [~10m]

### Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp
- Same conversion in the game-layer extensions, or the weakness survives at the top level: `FrameInterpolate::Crcs` (lines 534–549), `FramePostRender::Crcs` (lines 612–627), and `Frame::Crcs()`'s `interpolate ^ postRender` combine (line 692). Without this, identical corruption mirrored across the two halves (or across two game fields) still cancels [~10m]

## Critical files
- `Engine/Source/Frame/Alignments.cpp`
- `Engine/Source/Frame/FrameBase.cpp`
- `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp`

## Out of scope
- Changing any collection's own CRC computation (including the two-target XOR helper `common::Crc(value, rCrc, rSharedCrc)` at `Crc.h:123-130` that collection `Crcs()` methods feed — collection-internal mixing is a separate, larger pass if ever wanted)
- The hash function itself (`common::Crc` internals — already hardened in the landed Common determinism session)
- `LogDifferences` (own plan: `Architecture_LogDifferencesServerCollections.md`, same file — co-schedule)

## Notes
- **Determinism/CRC exposure: shared CRC values change.** Client and server must be rebuilt together (always true). **Replay `.checksums` files persist these values**: `DifferenceStreamWriter` records `Frame::Crc()` per tick into `<replay>.checksums` and `DifferenceStreamReader::ValidateChecksum` (`DifferenceStream.h:271-314`) compares on replay — old replays will *play* fine (the differences stream is input data, unaffected) but will log a `kError` CRC mismatch every tick, so they must be re-recorded. No graceful-reject is needed (mismatch is log-only, no abort), and `Frame::kiVersion` is not bumped (serialized state layout unchanged). Same accepted class as the landed `DeterministicSinCos`/`Crc` byte-fold sessions; no baked `.pack` impact.
- Network desync validation (`sharedCrc` in server broadcasts vs client `ReconcileReplayCrc`) is runtime-only and self-consistent after a joint rebuild.
- Shares `FrameBase.cpp` with `Architecture_LogDifferencesServerCollections.md` — land in one session.

## Verification Notes (2026-06-10)
- Verified both engine XOR sites verbatim (`Alignments.cpp:80-91` — XORs `uiKey` and `uiFlags` hashes per pair; `FrameBase.cpp:6-20, 70-85` — XORs scalars then folds `ServerCollections()` via `std::apply`).
- **Determinism reasoning held up; XOR is NOT load-bearing.** Checked every consumer: per-coord `Crcs()` runs single-threaded on its dispatch worker (`FrameTick.cpp:74`), on the reconcile thread (`ReconcileReplayTick.cpp:171`), at transfer harvest (`ServerTransferManager.cpp:242`), and on client full-state receive (`ClientDataReceiver.cpp:90`). No site accumulates CRCs across threads or in nondeterministic order; the alignments vector is insertion-sorted via `lower_bound` (`Alignments.cpp:25-34`).
- **Two corrections made**: (1) the original Notes claimed "the desync CRC is runtime-only" — false; replay `.checksums` files persist per-tick `Frame::Crc()` values (`DifferenceStream.h:35,47,110-118`). Effect on old replays is per-tick `kError` log spam, not a load failure — "replays reset" stands, the mechanism is now stated accurately. (2) The Design omitted the game-layer XOR extensions (`Frame.cpp:534-549, 612-627, 692`); fixing only the base classes would leave even-multiplicity cancellation at the top-level `interpolate ^ postRender` combine and among game fields. Added as a third Design item with matching Critical-files entry.
- `common::kCrcMultiplier` exists (`Crc.h:50`) and the proposed fold matches the existing `(crc ^ byte) * kCrcMultiplier` convention (`Crc.h:57-67`).
- Both pre-staged grill questions are now answered by the above; the remaining open decision is only the exact fold expression.
