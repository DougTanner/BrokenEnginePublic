# Refactor: DataPacker Texture Utility Dedup (zlib, Mip-Size)

## Context
Source: /external-refactor-clean on `DataPacker/Source/ExportJobs` (recursive). Two small utilities are each implemented three times across the texture TUs — classic DRY targets with an obvious survivor.

## Design

### Single zlib-compress helper
- Three copies: `ZlibCompress` (`ExportTexture.cpp:32-41`), inline in `Texture::Save` (`Texture/Texture.cpp:604-608`), inline in `MigrateLegacyIntermediates.cpp:192-197`. Keep `ZlibCompress` as the survivor, move it somewhere all three TUs reach (e.g. declared in `Texture/Texture.h`), delete the inline copies [~20m]

### Single mip-chain-size helper
- Three copies of the mip-chain size-summation loop: `ComputeUncompressedTextureSize` (`ExportTexture.cpp:43-55`), inline in `Texture::Export` (`Texture/Texture.cpp:494-502`), inline in `MigrateLegacyIntermediates.cpp:153-161`. Same treatment — one named helper, three call sites [~20m]

## Critical files
- `DataPacker/Source/ExportJobs/ExportTexture.cpp`
- `DataPacker/Source/ExportJobs/Texture/Texture.{h,cpp}`
- `DataPacker/Source/ExportJobs/Texture/MigrateLegacyIntermediates.cpp`

## Out of scope
- The texture-intermediate *read* helper (magic-vs-legacy branch ×3) — `Architecture_PayloadLayoutSingleSource.md` owns it; if both plans land in one session, share the same home for the helpers.
- zlib usage outside these three TUs.
- Any change to compression level/parameters — bytes identical.

## Notes
- No determinism/pack-byte exposure — identical zlib parameters and size math, just one definition.
- File-group overlap: `ExportTexture.cpp` and `Texture/*.cpp` are also touched by `Refactor_ExportTextureMechanics.md` and `Architecture_PayloadLayoutSingleSource.md` — co-schedule.

## Verification Notes
- All six copies verified at the cited lines; zlib parameters identical across the three (`Z_BEST_COMPRESSION` — `Texture.cpp` spells it `kiZlibLevel`, defined as `Z_BEST_COMPRESSION` at :34). Minor shape differences to absorb in the survivor: `ZlibCompress` returns a size-trimmed vector while `Texture::Save` (:604-608) keeps `uiCompressedSize` separate and writes exactly that many bytes (equivalent output); `MigrateLegacyIntermediates.cpp:192-197` likewise trims (:197).
- Mip-size copies: `ComputeUncompressedTextureSize` (:43-55) and `MigrateLegacyIntermediates.cpp:153-161` take an explicit mip count; `Texture::Export`'s inline loop (:494-502) iterates `mData.size()` — the shared helper should take `(vkFormat, width, height, mipCount)` and `Export` passes `mData.size()`.
- There is a fourth mip-size loop in `Main.cpp`'s `LoadBc7AsFloatPixelsMip0` (:242-250), BC7-hardcoded rather than `common::SizeInBytes`-based — fold it in only if `Architecture_PayloadLayoutSingleSource.md`'s shared texture-intermediate reader lands in the same session (that plan owns the Main.cpp site); otherwise leave to avoid cross-plan churn.
