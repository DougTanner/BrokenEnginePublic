# DataFile.h — Padding Math, Version Coverage, and Layout-Lock Hardening

## Context

`Common/DataFile.h` defines the `.pack` binary format: 16-byte-aligned, type-tagged
`ChunkHeader` (a union of font/scene/island/model/shader/texture/audio headers), the
`DataHeader` (magic + version + chunk count), and the offline `AlignOutputStream` padding
helper. The output is written by DataPacker (`DataPacker/Source/Main.cpp`,
`BakeIslandIntermediates.cpp`, `BakeIslands.cpp`) and memory-mapped + CRC-verified at load.

A six-lens review of the header surfaced several findings. After validating each against the
real source, three are genuine correctness/format-integrity bugs (padding math, version
coverage, hidden compiler padding in hand-padded structs) plus a cluster of cheap
layout-lock / init hardening items. The rest were dropped (see DROPPED in the validation
return) and the union indeterminate-tail-bytes Critical is owned by a theme plan
(`Determinism_IndeterminateBytes.md`) — not duplicated here.

## Design

All work is confined to `Common/DataFile.h` (plus one doc-accuracy edit). No `.pack`
*layout* changes are made — only padding math, compile-time asserts, an initializer, and a
doc correction. None of these change `sizeof(ChunkHeader)`, so no format version bump is
required by these edits themselves.

### 1. Correct the padding math in `AlignOutputStream` — `DataFile.h:24-32` (effort: 1)

Current:
```cpp
int64_t iBytesToAlign = kiAlignmentBytes - (rFileStream.tellp() % kiAlignmentBytes);
if (iBytesToAlign > 0 && iBytesToAlign < kiAlignmentBytes) { ... write ... }
```
The expression relies on the `< kiAlignmentBytes` branch guard to swallow the
already-aligned case (where `16 - 0 == 16`), and a non-positive/`pos_type(-1)` offset (a
stream whose write position is undefined) yields a wrong byte count (`-1 % 16 == -1` →
`iBytesToAlign == 17`) that is silently dropped, leaving the `.pack` misaligned with no
diagnostic and a malformed file produced offline.

Fix the **modulo math** so the result is always the correct `[0, kiAlignmentBytes)` padding
count, and assert the stream position is valid before computing it:
```cpp
const std::streamoff iPos = rFileStream.tellp();
ASSERT(iPos >= 0);
const int64_t iBytesToAlign = (kiAlignmentBytes - (static_cast<int64_t>(iPos) % kiAlignmentBytes)) % kiAlignmentBytes;
if (iBytesToAlign > 0) { rFileStream.write(&kpcPadding[0], iBytesToAlign); }
```
The trailing `% kiAlignmentBytes` collapses the aligned case to `0` (removing reliance on
the `< kiAlignmentBytes` guard), and capturing `tellp()` into an explicit `std::streamoff`
removes the fragile `fpos`→integer arithmetic. Framed as **correct padding math**, not added
error handling — the `ASSERT` only catches the undefined-position case that already produces
a corrupt file today.

### 2. Make the format version cover header *layout*, not just `sizeof` — `DataFile.h:318` (effort: 2)

`kiVersion = 46 + sizeof(ChunkHeader)`. `sizeof(ChunkHeader)` is dominated by
`char pcPath[MAX_PATH]` (line 295) plus the union, whose size is its **largest** member.
Reordering or shrinking the fields of a *non-largest* union member (e.g. `SceneHeader`,
`IslandHeader`, `FontHeader`) — or of `ModelVertex`/`Character` which are not in the union at
all — leaves `sizeof(ChunkHeader)` unchanged, so the version does **not** bump, and an old
`.pack` is silently misinterpreted by a new build. The auto-bump is therefore incomplete.

Add `static_assert`s on `sizeof` (and key `offsetof`s) of every header struct near the
version definition so any layout change is a compile error until the author updates both the
asserts and the manual `46`. This is the KISS choice (matches the codebase
"static_assert over runtime" rule) and makes the "any layout change bumps the version" claim
true by construction. Document next to `kiVersion` that any header layout edit requires
incrementing the manual constant.

### 3. Lock hand-padded struct layout; eliminate hidden compiler padding — `DataFile.h:77-88`, `:127-135`, `:150-157` (effort: 2)

`SceneHeader` (line 77), `ModelNode` (line 127), and `MaterialInfo` (line 150) use manual
`uiPad[]` arrays to control layout, but the manual padding is **insufficient** and unverified.
`SceneHeader`: `uint32 + uint32 + bool + uint8[3]` = 12 bytes, then `crc_t modelCrc`
(8-byte type, requires 8-byte alignment) — the compiler inserts **4 hidden bytes** at
offset 12 to align `modelCrc` to offset 16. Those 4 bytes are indeterminate and are written
to the file and folded into any CRC, contradicting what the `uiPad[3]` comment implies.

For each affected struct: add a `static_assert(sizeof(...) == N)` to lock the size, and make
the padding explicit (either expand the `uiPad` to fully cover the gap, e.g. an explicit
`uint8_t uiPad2[4] {}` before `modelCrc`, or reorder so the 8-byte `crc_t` leads). The
`{}`-initialized explicit pad guarantees the gap bytes are deterministically zero rather than
indeterminate. (Cross-reference: the *union-tail* indeterminate bytes are a separate concern
owned by `Determinism_IndeterminateBytes.md`; this item is only about *inter-member compiler
padding inside individual structs*.)

### 4. Cheap compile-time invariant + init hardening (effort: 1)

- `the prose invariant on MeshData::kiMaxMeshes` — `DataFile.h:163`: convert the comment
  "must be >= 2 * SceneHeader::kiMaxMaterials" into
  `static_assert(MeshData::kiMaxMeshes >= 2 * SceneHeader::kiMaxMaterials);`. Holds today
  (512 >= 256); the assert prevents a latent buffer overrun if either constant drifts.
- `ChunkLocation::crc` — `DataFile.h:36`: add `= 0`. It is the only member of `ChunkLocation`
  without an initializer (siblings `uiOffset`/`uiSize` are `= 0`); `ChunkLocation` instances
  are written to the manifest, so an uninitialized `crc` risks indeterminate bytes in output.
- `ModelVertex` / `Character` — `DataFile.h:217-226`, `:324-346`: add
  `static_assert(sizeof(...) == <sum of members>)` to confirm padding-free layout. The
  `std::hash<ModelVertex>` specialization (line 350) hashes raw bytes via `Crc(rVertex)`,
  while `operator==` compares fields; the assert guarantees no padding can make two
  `operator==`-equal vertices hash differently and break the mesh-dedup `unordered_map`.

## Critical files

- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\DataFile.h` — all code edits.
- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\CLAUDE.md` — line 9 doc-accuracy edit:
  qualify "any header layout change bumps the version automatically" to reflect that the
  auto-bump only catches `sizeof` changes (now backstopped by the new layout `static_assert`s).
- Consumers to re-verify (no edits expected): `DataPacker/Source/Main.cpp` (writer +
  `AlignOutputStream` callers), `BakeIslandIntermediates.cpp`, `BakeIslands.cpp`.

## Out of scope

- The union indeterminate-tail-bytes Critical (NSDMI members / zeroing the full union
  footprint / `static_assert(is_trivially_copyable)`) — owned by
  `Documents/Plans/Common/Determinism_IndeterminateBytes.md`. Do NOT touch the union here.
- Adding runtime bounds/trust validation to the `.pack` read path (negative/huge `iSize`,
  unbounded `iChunkCount`). Dropped per the project "assume parameters valid / no defensive
  validation" policy; the format is locally produced and gated by magic + version.
- Reorganizing the format: moving the elevation constants out of `DataFile.h`, removing the
  inline `pcPath[MAX_PATH]`, replacing `WAVEFORMATEX` with fixed-width fields, or changing
  the `int64_t` magic to `uint64_t`. These are design/perf/portability preferences, not bugs,
  and any field-width or member change would force a format version bump — out of scope.
- Integer-type consistency, `operator==` call-form consistency, and digit-separator/`ull`
  literal style — cosmetic.

## Acceptance criteria

- `AlignOutputStream` writes the correct padding count for every input offset, emits zero
  bytes when already aligned without depending on the `< kiAlignmentBytes` guard, and asserts
  on a negative/undefined stream position.
- New `static_assert`s lock `sizeof` (and relevant `offsetof`) of every header struct;
  changing any header's internal layout produces a compile error pointing at the version /
  assert block until the author updates it.
- `SceneHeader`, `ModelNode`, and `MaterialInfo` have no hidden indeterminate compiler
  padding (gaps are explicit `{}`-initialized members or eliminated by reorder), confirmed by
  `static_assert`.
- `MeshData::kiMaxMeshes >= 2 * SceneHeader::kiMaxMaterials` is a compile-time guarantee;
  `ChunkLocation::crc` is initialized; `ModelVertex`/`Character` are asserted padding-free.
- DataPacker and Engine compile with no new warnings; `sizeof(ChunkHeader)` is unchanged by
  these edits (no unintended format version change).
- `Common/CLAUDE.md` no longer overstates the version auto-bump guarantee.

## Notes

- The Critical finding "union members with NSDMIs / non-trivially-copyable types leave
  indeterminate tail bytes written to the memory-mapped, CRC-verified `.pack`" is intentionally
  excluded here — it is owned by the theme plan
  `Documents/Plans/Common/Determinism_IndeterminateBytes.md`. This plan's struct-padding item
  (3) is a distinct, narrower concern (inter-member compiler padding inside individual
  structs) and does not modify the union.
- The original report cited the engine read path as `Engine/Source/Data/Data.cpp`; that file
  does not exist on this branch. The real consumer is `Engine/Source/File/FileManager.cpp`
  (e.g. line 340 `reinterpret_cast<common::ChunkHeader*>(&rPackBytes[...])`, line 342
  `+ common::kiChunkDataOffset`). The header-level findings above stand independent of the
  consumer; the read-path bounds-validation finding was dropped on policy grounds regardless.
- Verified the `.pack` writer: `DataPacker/Source/Main.cpp:395-430` opens distinct manifest
  (`.manifest`) and pack (`.pack`) `std::fstream`s and calls `AlignOutputStream` on each — no
  same-path collision. Report finding L7 ("callers may use `std::ofstream`") is a phantom:
  every real caller passes `std::fstream`. Dropped.
