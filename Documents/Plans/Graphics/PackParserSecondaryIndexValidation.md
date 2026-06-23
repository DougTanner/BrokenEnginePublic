# Tier-4 .pack Parser Secondary-Index and Chunk-Byte Validation

## Context

Follow-up from the landed **Deserialized Count/Capacity Trust-Boundary Validation** change, which (per a "fold all 13" execution decision) extended trust-boundary validation to the runtime `.pack` chunk parsers. That change validated the parsers' **header counts** against the `Common/DataFile.h` `kiMax*` structural maxima (and, for `TextManager`, the chunk byte size), throwing `common::CorruptStreamException` caught at the asset-load boundary (boot hard-fail / loading-thread soft-fail). Two gaps remained, surfaced by the final audit passes:

1. **AnimationData secondary indices are unvalidated** (`Engine/Source/Graphics/AnimationData.cpp`). The header-count guard bounds the array *sizes*, but indices stored *inside* the now-bounded records are not checked, so a corrupt/tampered chunk whose counts are in range can still corrupt memory at boot:
   - `:81` — `mbAnimatedNodes[iAnim][mpChannels[iCh].uiNodeIndex]` writes into `bool[kiMaxAnimations][kiMaxNodes]` (`AnimationData.h`); `uiNodeIndex` is an unvalidated `uint16_t` (0..65535) → OOB **write** up to ~64 KB past the array. (Most serious — an out-of-bounds write, not just a read.)
   - `:79` / `:296` — `clip.uiChannelStart` / `uiChannelCount` index `mpChannels` with no bound vs the validated `uiChannelCount` → OOB read.
   - `:304-310` — `p{Translations,Rotations,Scales}[rChannel.uiNodeIndex]` (workbuffer `[kiMaxNodes]`) — same unvalidated index.
   - `:381-383` — `pWorldMatrices[mpSkinJointToNode[i]]` indexes `[kiMaxNodes]` with an unvalidated on-disk joint→node entry.
   - (`mpNodes[i].iParentIndex < i` is already enforced via a throwing ASSERT at `:61`, so node parent indices are covered.)

2. **`ModelPipeline` / `AnimationData` bound counts against structural maxima, not the chunk's actual byte size.** `ModelPipeline::Create` (`uiTextureCount`/`uiMaterialCount` vs `SceneHeader::kiMax*`) and `AnimationData::Load` (channels/keyframes vs the 16M `kiMaxDeserializedCapacity` ceiling) reject only oversized-vs-max counts. A count that is `<= max` but exceeds what the chunk's bytes can actually back walks the alias pointers / `std::span(pTextureCrcs, uiTextureCount)` off the chunk into adjacent pack bytes (garbage) or unmapped memory (fault). Boot-tier (faults route to the crash handler), so this is a robustness/consistency tightening rather than a correctness defect — `TextManager` already bounds against the chunk byte size and is the model to mirror.

All client-only (`BT_CLIENT`); no determinism/CRC/`kiVersion` exposure. The trigger is a corrupt or tampered `.pack` on disk (our own assets); the worst case (item 1, the `uiNodeIndex` OOB write) is a memory-corruption class worth closing now that the trust boundary is explicit.

## Design

- **AnimationData secondary indices**: after the existing header-count guard, validate every on-disk index against its target bound before use, throwing `common::CorruptStreamException("AnimationData::Load")` on violation — `channel.uiNodeIndex < skeleton.uiNodeCount`; `clip.uiChannelStart >= 0 && clip.uiChannelStart + clip.uiChannelCount <= uiChannelCount`; each `uiKeyframeStart`/count within `uiKeyframeCount` / `uiCubicKeyframeCount`; each `mpSkinJointToNode[i] < uiNodeCount`. Validate once at load (where the records are first walked) so the hot per-frame animation path stays unchecked. Confirm the exact record field names/layout against `Common/DataFile.h` + `AnimationData.h` at execution.
- **Chunk-byte bounds (`ModelPipeline` / `AnimationData`)**: where the parser has the chunk byte size (`ModelPipeline::Create` has the scene chunk; `AnimationData::Load` currently receives only a raw `const std::byte*` — thread the chunk size in from `LoadAnimationDataFromEagerChunks`, which holds `rChunk.pHeader->iSize`), add a "computed end offset <= chunk bytes" check mirroring `TextManager`'s exact-offset guard, so a `<=`-max-but-oversized count is rejected before the pointer walk.

## Critical files

- `Engine/Source/Graphics/AnimationData.cpp` (+ `AnimationData.h` for the array bounds; `LoadAnimationDataFromEagerChunks` to pass the chunk size)
- `Engine/Source/Graphics/Objects/ModelPipeline.cpp`

## Out of scope

- The header-count guards (already landed — this plan adds the secondary-index + chunk-byte layer on top).
- The istream collection / save / replay / network readers (already validate counts and are caught at their boundaries).
- `TextManager` (already bounds against the chunk byte size — it is the model).
- Any `.pack` format / `kiVersion` change (validation only; no DataPacker re-export).

## Acceptance criteria

- A `.pack` animation chunk whose header counts are in range but whose internal `uiNodeIndex` / `uiChannelStart` / joint→node entries point out of bounds is rejected through `CorruptStreamException` instead of writing/reading out of bounds.
- A `<=`-max-but-oversized texture/material/keyframe count is rejected before the pointer walk.
- Every valid shipped asset still loads (bounds verified against DataPacker emission).

## Notes

- **Invariant exposure**: client-only `.pack` parse paths; behavior changes only for corrupt/tampered chunks. No determinism / CRC / `kiVersion` / protocol exposure. No DataPacker re-export (no format change) — but verify each new bound against what DataPacker actually emits (`DataPacker/Source/ExportJobs/`) so a valid asset is never false-rejected.
- The catch boundary is already in place: these throws are the same `CorruptStreamException` type at the same call sites (`AnimationData::LoadAnimationDataFromEagerChunks` boot catch; `ModelPipeline` via `DynamicPipelines`), so no new catch is needed.
- **Pre-staged grill decision**: whether to thread the chunk byte size into `AnimationData::Load`'s signature (enables the chunk-byte bound) vs. add only the secondary-index checks (which close the OOB-**write** class without a signature change). The index checks are the higher-value half.
