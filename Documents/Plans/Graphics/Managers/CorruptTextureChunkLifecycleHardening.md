# Harden the Corrupt-Texture-Chunk Soft-Fail Lifecycle

## Context

The upload thread's per-chunk soft-fail catch (`TextureUploadManager::UploadThread`, `Engine/Source/Graphics/Managers/TextureUploadManager.cpp:306-328`) marks a corrupt chunk `kReady` zero-filled and claims "No GPU image was created (dimensions validated first)". Two downstream gaps:

1. **Stranded `vkImage`.** A throw *after* `CreateTransferImage` (anything in the parse/submit path that throws non-`DeviceLostException` — e.g. `bad_alloc`, a throwing VK check in `SubmitChunkUpload`) lands in the same catch with `rLazyChunk.vkImage` still set; the chunk is marked `kReady` without adoption. If per-island LRU eviction later hits it, `EvictTemplate` → `ResetTextureChunkStates` (`FileManager.cpp:774-776`) nulls the chunk handles **without destroying** → unreachable VMA allocation → VMA "allocations not freed" assert at device teardown.
2. **Invalid descriptor write for never-adopted textures.** `TextureDescriptors::WriteArrayElementFromLive` (`TextureDescriptors.cpp:218`) falls back to the white placeholder only for `pTexture == nullptr`. A soft-failed island channel chunk leaves a non-null `Texture` whose `mVkImageView` is `VK_NULL_HANDLE`, and `RestorationSweep`'s `bAllReady` gate (`IslandTerrainResidency.cpp:264`, checks only `>= kReady`) patches that null view into the bindless Set-1 element.

## Design

- First, pin down which failure paths in the upload parse/submit sequence can actually throw a non-device-loss exception after image creation (`CHECK_VK` policy: device-lost throws `DeviceLostException`; confirm whether other VK failures throw or just log+break). If none can, gap 1 shrinks to a comment truth-up — gap 2 stands regardless (soft-fail before image creation still produces the non-null-Texture/null-view state).
- Gap 1: in the catch, when `rLazyChunk.vkImage != VK_NULL_HANDLE`, `vmaDestroyImage` + null the chunk handles (safe — the image is transfer-thread-private until `kGpuUploadComplete`); fix the comment.
- Gap 2: extend the `WriteArrayElementFromLive` fallback condition to `pTexture == nullptr || pTexture->mVkImageView == VK_NULL_HANDLE`.

## Critical files

- `Engine/Source/Graphics/Managers/TextureUploadManager.cpp` — `UploadThread` catch block
- `Engine/Source/Graphics/Managers/TextureDescriptors.cpp` — `WriteArrayElementFromLive`
- `Engine/Source/Frame/IslandTerrainResidency.cpp` — `RestorationSweep` `bAllReady` gate (read-only context)

## Out of scope

- The `WaitIdle` teardown deadlock/race — companion plan `TextureUploadTeardownRaces.md`.
- The sim-side consequence of corrupt chunks (silent desync) — `Network/PackIntegrityHandshake.md`.
- Changing the soft-fail policy itself (zero-filled `kReady` chunks stay; this plan only stops them leaking GPU objects and null views).

## Coordination

- `Documents/Plans/Graphics/Managers/Architecture_BindlessSlotLifecycle.md`: mandatory reciprocal pipeline-cluster exclusion; never interleave because BindlessSlotLifecycle executes alone.

## Notes

- Client/graphics-only; no determinism/CRC/`kiVersion`/wire exposure. Reachable only downstream of a corrupt-chunk soft-fail, so severity is hardening-tier.
- Overlaps `TextureDescriptors.cpp` with the live `Graphics/Managers/Architecture_BindlessSlotLifecycle.md` (which reshapes `WriteArrayElementFromLive` into the slot registry) — land this small fix **before** it, or fold gap 2 into the registry work; never interleave.
- Co-schedule with `TextureUploadTeardownRaces.md` (same file).
- No grill decisions; the CHECK_VK throw-behavior check in Design step 1 resolves the only open question at execution.
