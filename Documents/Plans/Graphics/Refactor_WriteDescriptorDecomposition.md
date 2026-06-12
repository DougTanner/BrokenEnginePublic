# Refactor: PipelineDescriptorWriter Write-Path Decomposition

## Context

Source: /external-refactor-clean on `Engine/Source/Graphics/Objects` (non-recursive).
`PipelineDescriptorWriter.cpp` (671 lines) concentrates the directory's duplication:
`WriteModelDescriptor` is four copy-pasted image-info/write/register blocks behind a 12-parameter
signature, a registration predicate is repeated six times, and `Write` is 276 lines at 5-level nesting.
All cold-path (startup / device-loss / settings recreate), so this is pure mechanical extraction.

## Design

### Engine/Source/Graphics/Objects/PipelineDescriptorWriter.cpp

- Introduce a `DescriptorWriteCursor` struct (anonymous namespace) bundling the seven write-cursor
  parameters of `WriteModelDescriptor` (`:45` — `pVkWriteDescriptorSets`, `riDescriptorCount`,
  `pVkDescriptorImageInfos`, `riImageInfoCount`, `iMaxImageInfos`, `pVkDescriptorBufferInfos`,
  `riBufferInfoCount`); shrinks the 12-param list and `Write`'s local clutter. [~30m]
- Extract `PushCombinedImageSamplerWrite(Pipeline&, const PipelineInfo&, int64_t iFramebuffer,
  DescriptorWriteCursor&, common::crc_t registerCrc, Texture* pTexture)` replacing the three identical IBL
  blocks in `WriteModelDescriptor` — irradiance (`:84-104`, `kIrradianceCrc` + `mTextureMap`), prefiltered
  (`:106-126`, `kPrefilteredCrc` + `mTextureMap`), lutBRDF (`:128-147`, CRC 0 +
  `mTextureCache.mPbrLutBrdfTexture`). The standalone-sampler (`:50-70`) and bindless (`:72-82`) blocks
  are genuinely distinct and stay. ~63 lines become 3 calls. [~30m]
- Extract `ShouldRegisterBinding(...)` for the 6×-repeated predicate
  `iFramebuffer == 0 && BindingExistsInShaderLayout(...) && !BindingIsInSet0(...)`
  (`:66`, `:101`, `:123`, `:144`, `:427`, `:470`). Note `:470` carries one extra conjunct
  (`rDescriptorInfo.flags & kCombinedSamplers`) — keep it at the call site, outside the helper. [~15m]
- Decompose `PipelineDescriptorWriter::Write` (`:270-545`, 276 lines, 5-level nesting: framebuffer loop →
  descriptor loop → flag else-if chain → inner `k` loop → per-source if-chain `:443-467`; registration
  block `:470-517` also 5 deep): extract the per-flag branch bodies into named statics sharing the cursor
  struct — `WriteBufferDescriptor` (`:390-413`), `WriteStandaloneSampler` (`:414-431`),
  `WriteCombinedSamplers` (`:439-523`). `Write` becomes the loop skeleton + dispatch. [~45m]

## Critical files
- `Engine/Source/Graphics/Objects/PipelineDescriptorWriter.cpp` (and `.h` only if signatures move)

## Out of scope
- Moving registration ownership out of this file — `Graphics/Architecture_PipelineRegistrationOwnership.md`
  (lands its option first or after; the extracted helpers make either option a smaller diff).
- The set-index resolver dedup (`BindingIsInSet0`/`RouteWritesBySet`) —
  `Graphics/Architecture_DescriptorSetIndexDedup.md` (co-schedule; shares this file).
- Dropping the redundant `PipelineInfo` parameter — `Graphics/Refactor_PipelineCreatorDecomposition.md`
  owns that cross-file signature change; land it with or after this plan to avoid double churn.
- Any descriptor content/order change — writes must stay byte-identical.

## Acceptance criteria
- `WriteModelDescriptor` ≤ ~60 lines with one combined-sampler helper; no function in the file exceeds
  ~100 lines or 4 nesting levels; the registration predicate exists once; client builds clean and a boot +
  settings-recreate smoke renders identically.

## Notes
- Cold-path only (record-once contract, `Graphics/CLAUDE.md:9`); the whole recreate flow runs under
  `Graphics::Create`'s blanket `ScopedSuppressAllocationTracking` (`Graphics.cpp:264-268`) so helper
  extraction has no allocation-tracker exposure. Client-only; no determinism/CRC, `kiVersion`, replay, or
  network exposure. No grill decisions.
- Co-schedule with `Refactor_PipelineCreatorDecomposition.md` and `Architecture_DescriptorSetIndexDedup.md`
  (shared files/functions — one session, refresh citations between landings).

## Verification Notes
Verified: all cited paths/lines/symbols re-checked against source (file is 671 lines as claimed).
- `WriteModelDescriptor` at `:45` with exactly 12 parameters; the seven cursor parameters are as listed.
  Block ranges exact: standalone sampler `:50-70`, bindless array `:72-82`, irradiance `:84-104`,
  prefiltered `:106-126`, lutBRDF `:128-147`. The three IBL blocks are structurally identical
  (image-info {kSamplerRepeat sampler, view, SHADER_READ_ONLY} → combined-image-sampler write → push →
  registration predicate → `RegisterTextureBinding(crc, …, pTexture)`) differing only in
  (CRC, Texture source) — `kIrradianceCrc`/`mTextureMap`, `kPrefilteredCrc`/`mTextureMap`,
  CRC 0/`mTextureCache.mPbrLutBrdfTexture` — so the proposed `(registerCrc, pTexture)` parameterization is
  exactly sufficient. The standalone-sampler (SAMPLER type, null view) and bindless (writes
  `mImageInfos.data()` directly, no per-write image info, no registration) blocks are genuinely distinct.
- Corrected the predicate count: six occurrences, not seven (`:66,:101,:123,:144,:427,:470`); `:470` adds a
  `kCombinedSamplers` conjunct that stays outside the helper.
- `Write` spans `:270-545` (276 lines); nesting chain and sub-ranges exact (`:390-413` buffer branch,
  `:414-431` standalone sampler, `:439-523` combined-samplers/storage-images including the `:443-467` k-loop
  with per-source if-chain and the `:470-517` registration block).
- Allocation note verified: the whole path runs under `Graphics::Create`'s
  `ScopedSuppressAllocationTracking` (`Graphics.cpp:264-268`); the per-Write image-info buffer is
  workbuffer-backed (`:348`) — the cursor struct must carry that raw pointer, not own it.
- Sole `Write` caller is `Pipeline.cpp:144`; `TextureDescriptors.cpp` consumes only
  `BindingExistsInShaderLayout` (`:253,:272`), so decomposition cannot ripple beyond this TU.
