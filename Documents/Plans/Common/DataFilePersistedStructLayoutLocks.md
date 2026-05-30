# DataFile.h: Layout-Lock the Remaining Persisted `.pack` Structs

## Context

The just-completed determinism-bytes session added `sizeof`/`offsetof` layout-lock `static_assert`s
to several persisted `.pack` header structs in `Common/DataFile.h` (`SceneHeader`, `ModelNode`,
`MaterialInfo`, `Character`, `ModelVertex`, `FontHeader`, `IslandHeader`, `ModelHeader`,
`ShaderHeader`, `TextureHeader`, plus `static_assert(std::is_trivially_copyable_v<ChunkHeader>)`).

The intent of those locks is **version coverage**. `DataHeader::kiVersion` is
`46 + sizeof(ChunkHeader)`, so it auto-bumps only when a layout edit changes `sizeof(ChunkHeader)`
(an outer-field edit or a change to the *largest* union member). A layout edit that does **not**
change `sizeof(ChunkHeader)` — reordering/shrinking a non-largest union member, or changing a
non-union payload struct — slips past the auto-bump. The per-struct locks catch exactly that class:
when one fires, the author is forced to manually bump `kiVersion`. The comment block above
`kiVersion` documents this contract directly.

A session audit found the coverage is **incomplete**. Several structs that are *also* persisted into
the `.pack` (written by DataPacker via `memcpy` sized by `sizeof`, read back by the engine via
`reinterpret_cast`) still lack a layout-lock `static_assert`. A silent layout change to any of them
desyncs writer vs. reader with no compile-time guard, and `kiVersion` would not auto-bump because
none of them is the largest union member (or they are not in the union at all).

The persisted structs missing a lock, all verified against current source:

1. **Animation-stream structs** (highest value). The animation section of a scene chunk is a tightly
   packed byte stream. DataPacker writes it in `ExportScene::ExportAnimationData` (the run of
   `std::memcpy(..., sizeof(common::X))` blocks in `DataPacker/Source/ExportJobs/ExportScene.cpp`,
   ~lines 780-829), and the engine reads it back by advancing a `const std::byte*` and
   `reinterpret_cast`ing each segment in `engine::AnimationData::Load`
   (`Engine/Source/Graphics/AnimationData.cpp`, ~lines 11-39). Both sides step the pointer by
   `sizeof(common::X)`, so the two `sizeof`s must agree — but nothing pins either side to a constant.
   The structs (in `Common/DataFile.h`):
   - `AnimationKeyframe` (~:99) — `memcpy`'d by ExportScene, cast by `AnimationData::mpKeyframes`.
   - `AnimationKeyframeCubic` (~:106) — `memcpy`'d by ExportScene, cast by `AnimationData::mpCubicKeyframes`.
   - `AnimationChannel` (~:115) — `memcpy`'d by ExportScene, cast by `AnimationData::mpChannels`.
   - `AnimationClip` (~:125) — `memcpy`'d by ExportScene, cast by `AnimationData::mpAnimations`.
   - `Skeleton` (~:147) — embedded by value inside `AnimationHeader`; its `uiNodeCount`/`uiSkinJointCount`
     drive the reader's pointer arithmetic for the trailing `ModelNode`/`skinJointToNode`/inverse-bind
     segments, so a layout drift here mis-strides the entire animation section.
   - `AnimationHeader` (~:193) — `memcpy`'d as the first block in the section and read back by value
     into `AnimationData::mHeader`; embeds `Skeleton`.

2. **`MaterialShaderData`** (~:205). Written into the scene chunk payload by ExportScene
   (`std::memcpy(pAnimData... )` is preceded by the material-data region; the runtime offset math in
   `Engine/Source/File/FileManager.cpp` ~:366 sizes the material sub-region by
   `sizeof(common::MaterialShaderData)` to locate the animation data that follows). Its own comment
   says it "must exactly match `PbrMaterialLayout` in engine," making it a cross-boundary layout
   contract on top of the writer/reader stride contract.

3. **`AudioHeader`** (~:299). A union member of `ChunkHeader` (`audioHeader`), written by
   `ExportAudio` (`pHeader->audioHeader.waveFormat.* = ...`,
   `DataPacker/Source/ExportJobs/ExportAudio.cpp` ~:67-73) and read back by the audio voices
   (`rLazyChunk.header.audioHeader.waveFormat.*` in `StreamingVoices.cpp`, `StreamingVoice.cpp`,
   `StaticVoice.cpp`). **Lowest priority.** It wraps a single Win32 `WAVEFORMATEX`, a platform type
   unlikely to change; and it only escapes the `sizeof(ChunkHeader)` auto-bump because the
   `pcPath[MAX_PATH]` union member dwarfs it (so a change to `AudioHeader`'s size would not shift
   the union footprint). Include it for completeness but note the low severity.

This is the same class of latent format-desync hardening the prior session began; this plan finishes
the persisted-struct coverage. It is **pure additive `static_assert`s** — no struct layout changes, no
`.pack`/CRC/version change, fully compile-checked.

## Design

Add a `static_assert` immediately after each listed struct definition in `Common/DataFile.h`,
matching the exact form and message style already used by the locked structs (e.g.
`static_assert(sizeof(SceneHeader) == 24, "SceneHeader layout changed — bump DataHeader::kiVersion");`).
Add an `offsetof` lock only where a struct carries internal padding worth pinning, mirroring the
existing `SceneHeader`/`offsetof(SceneHeader, modelCrc) == 16` precedent.

The expected sizes below were computed by hand under x64 MSVC natural-alignment rules and
cross-checked against the already-landed locks (`ModelNode == 116` confirms `XMFLOAT4` has 4-byte
alignment in this build: `2 + 2 + 64 + 3*16 = 116`). **The implementer must confirm each literal by
building** — if a value differs from the prediction, use the compiler's reported size, not the
prediction.

### 1. Animation-stream structs

| Struct | Expected `sizeof` | `offsetof` worth pinning |
|---|---|---|
| `AnimationKeyframe` | 20 | (`f4Value` == 4 — pins the 4-byte pad after `fTime`) |
| `AnimationKeyframeCubic` | 52 | none required (all members contiguous at 4/20/36) |
| `AnimationChannel` | 12 | none required (naturally packed; `uint32_t`s land at 4/8) |
| `AnimationClip` | 76 | none required (`char[64]` then float/uint32) |
| `Skeleton` | 4 | none required (two `uint16_t`) |
| `AnimationHeader` | 24 | (`skeleton` == 20 — pins the embedded-`Skeleton` offset) |

For `AnimationKeyframe`, add the `offsetof(AnimationKeyframe, f4Value) == 4` lock because `fTime`
(4 bytes) is followed by padding-free placement of a 4-aligned `XMFLOAT4`; pinning the offset documents
that the keyframe stays at the 20-byte stride both `memcpy` and `reinterpret_cast` assume. For
`AnimationHeader`, pin `offsetof(AnimationHeader, skeleton) == 20` because the embedded `Skeleton`
position is what the reader implicitly relies on when it copies the header by value and then reads
`mHeader.skeleton.uiNodeCount`.

Message form per the existing convention, e.g.:

```cpp
static_assert(sizeof(AnimationKeyframe) == 20, "AnimationKeyframe layout changed — bump DataHeader::kiVersion");
static_assert(offsetof(AnimationKeyframe, f4Value) == 4, "AnimationKeyframe padding changed — keyframe stride no longer matches writer/reader");
```

### 2. `MaterialShaderData`

Expected `sizeof(MaterialShaderData) == 76` (five `uint8_t` + 3 pad → two `XMFLOAT4` at 8/24 → five
`int32_t` → four `float`). Add:

```cpp
static_assert(sizeof(MaterialShaderData) == 76, "MaterialShaderData layout changed — bump DataHeader::kiVersion and re-check PbrMaterialLayout");
```

Pin `offsetof(MaterialShaderData, f4BaseColorFactor) == 8` to document the 3-byte pad after the five
texture-index bytes (the boundary where the layout must match the engine's `PbrMaterialLayout`):

```cpp
static_assert(offsetof(MaterialShaderData, f4BaseColorFactor) == 8, "MaterialShaderData padding changed — PBR factor block no longer at offset 8");
```

### 3. `AudioHeader`

Because `AudioHeader` is a thin wrapper over the platform `WAVEFORMATEX`, asserting a literal byte
count (18 on current Win32) is platform-fragile — a future SDK or platform could redefine the type.
Assert against the wrapped type instead, which both locks the wrapper-adds-no-padding invariant and
survives a platform `WAVEFORMATEX` redefinition:

```cpp
static_assert(sizeof(AudioHeader) == sizeof(WAVEFORMATEX), "AudioHeader must wrap WAVEFORMATEX with no padding");
```

This is the only struct in this plan whose lock is intentionally **not** a literal `sizeof == N`,
for the platform-fragility reason above. Note in a one-line comment that `AudioHeader` is the
lowest-severity entry (non-largest union member; depends on a platform struct).

### KISS / scope notes

- Additive `static_assert`s only — one (occasionally two) lines per struct, placed directly under the
  struct, mirroring the existing locked-struct pattern in the same file.
- Do **not** reorder, repad, or otherwise touch any struct's members. The asserts pin the *current*
  layout; they do not change it.
- Do **not** bump `DataHeader::kiVersion`. None of these edits changes `sizeof(ChunkHeader)` or any
  struct layout, so the on-disk format is byte-identical; a version bump would needlessly invalidate
  every shipped `.pack` and every CRC. The whole point of the locks is to force a *future* author to
  bump it when they change a layout.
- No new error handling, runtime validation, or logging. Compile-time only.

## Critical files

- `Common/DataFile.h` — the only file edited. Add the `static_assert`s after these definitions:
  `AnimationKeyframe` (~:99), `AnimationKeyframeCubic` (~:106), `AnimationChannel` (~:115),
  `AnimationClip` (~:125), `Skeleton` (~:147), `AnimationHeader` (~:193), `MaterialShaderData` (~:205),
  `AudioHeader` (~:299). Match the message form of the existing `SceneHeader`/`ModelNode`/`MaterialInfo`
  locks in the same file.
- `DataPacker/Source/ExportJobs/ExportScene.cpp` — **read-only reference.** The
  `ExportScene::ExportAnimationData` `std::memcpy(..., sizeof(common::X))` block (~:780-829) and the
  `sizeof(common::MaterialShaderData)` material-region sizing (~:793) are the *writer* side of the
  stride contract these locks protect. Not edited.
- `Engine/Source/Graphics/AnimationData.cpp` — **read-only reference.** `AnimationData::Load`
  (~:11-39) is the *reader* side: it advances a `const std::byte*` by `sizeof(common::X)` and
  `reinterpret_cast`s each segment (`mpNodes`, `mpAnimations`, `mpMaterialInfos`, `mpChannels`,
  `mpKeyframes`, `mpCubicKeyframes`) and copies `mHeader` by value. Not edited.
- `Engine/Source/File/FileManager.cpp` — **read-only reference.** The eager-load offset math (~:366)
  sizes the material sub-region by `sizeof(common::MaterialShaderData)` to locate the trailing
  animation data. Not edited.
- `DataPacker/Source/ExportJobs/ExportAudio.cpp` — **read-only reference.** Writes
  `pHeader->audioHeader.waveFormat.*` (~:67-73), the `AudioHeader` writer. Not edited.

## Out of scope

- **Changing any struct's actual memory layout.** This plan is lock-only / additive `static_assert`s.
  If a predicted `sizeof` is wrong, fix the *assert literal* to match the compiler, never the struct.
- **Bumping `DataHeader::kiVersion`.** None of these edits changes `sizeof(ChunkHeader)` or any layout,
  so the `.pack` byte format and all CRCs are unchanged; a version bump is wrong here.
- **The structs already locked in the prior determinism-bytes session** — `SceneHeader`, `ModelNode`,
  `MaterialInfo`, `Character`, `ModelVertex`, `FontHeader`, `IslandHeader`, `ModelHeader`,
  `ShaderHeader`, `TextureHeader`, and the `is_trivially_copyable_v<ChunkHeader>` assert. They already
  have coverage; do not duplicate or modify them.
- **The `ChunkHeader` union canonical-zero / indeterminate-tail-byte work** — owned by
  `Common/Determinism_IndeterminateBytes.md`. This plan adds `sizeof`/`offsetof` locks only; it does
  not touch union zeroing or `Crc`/`Serialization` byte contracts.
- **`MeshData` / `JointMatrix`** — these are runtime GPU-buffer / SSBO layouts, not `.pack`-persisted
  payload structs (`MeshData` already carries a `kiMaxMeshes` capacity assert for a different reason).
  Not part of the persisted-format coverage gap.
- **The non-`sizeof` `AlignOutputStream` padding-math and `ChunkLocation::crc`-init items** — owned by
  the existing `Common/DataFile.md` plan; not duplicated here.
- All style findings in `DataFile.h` (Hungarian, brace placement, etc.) — owned by `code-style-review`.

## Acceptance criteria

- Each of `AnimationKeyframe`, `AnimationKeyframeCubic`, `AnimationChannel`, `AnimationClip`,
  `Skeleton`, `AnimationHeader`, `MaterialShaderData`, and `AudioHeader` has a `sizeof` (or, for
  `AudioHeader`, `sizeof == sizeof(WAVEFORMATEX)`) layout-lock `static_assert` directly beneath it,
  in the message form already used by the file's locked structs.
- `AnimationKeyframe`, `AnimationHeader`, and `MaterialShaderData` additionally carry the `offsetof`
  locks specified above (pad after `fTime`, embedded `Skeleton` offset, PBR-factor-block offset).
- Every project that compiles `Common/DataFile.h` (DataPacker, client, server) builds clean — the
  asserts pass against the current layout on first build (any literal that fails the build is corrected
  to the compiler-reported value, never by editing a struct).
- `DataHeader::kiVersion` is unchanged; no `.pack` regeneration or CRC rebake is required.

## Notes

- All symbols verified against current source: writer `ExportScene::ExportAnimationData` (the
  `sizeof(common::AnimationKeyframe/.../AnimationHeader)` `memcpy` run), reader `AnimationData::Load`
  (the matching `reinterpret_cast`/pointer-advance segments), `FileManager.cpp`'s
  `sizeof(common::MaterialShaderData)` offset math, and `ExportAudio`'s `audioHeader.waveFormat`
  writes. The persistence of each struct is confirmed, not assumed.
- The predicted sizes are hand-computed; the build is the source of truth. They are listed only to make
  the diff reviewable and to flag if a struct has unexpected padding.
