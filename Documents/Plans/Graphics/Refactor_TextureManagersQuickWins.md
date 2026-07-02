# Refactor: Texture Managers Quick Wins

## Context
Source: /external-refactor-clean on Engine/Source (recursive). Texture/render-target manager batch: one correctness-relevant validation asymmetry (corrupt-header overread on the integrated-GPU adopt path) plus mechanical cleanup. Deduped against the live texture-upload/corrupt-chunk/bindless/pool-reclaim plans.

## Design

### Engine/Source/Graphics/Managers/TextureManager.cpp
- **kDiskLoaded fallback validation gap (correctness)**: the same-queue-family fallback adopt (:522-542) `memcpy`s from `rLazyChunk.pData` using dims/mips from on-disk `ChunkHeader::textureHeader` bytes with no size validation, while the transfer-queue path validates via `TextureUploadManager::ValidateTextureDimensions` (`TextureUploadManager.cpp:377-398`) precisely so "the copy loop cannot read off pData". On devices without a separate transfer family (or via `HandleUploadEarlyOut`) a corrupt header drives the memcpy past `pData`. Share `ValidateTextureDimensions` (or equivalent dims-vs-`iDataSize` check) before the fallback `Create`, soft-failing to `kReady` like the upload thread. Distinct from the live `CorruptTextureChunkLifecycleHardening` plan (upload-thread catch + `WriteArrayElementFromLive`) [~30m]
- Drop the phantom `gpIslandTerrain != nullptr` guard (:216-219) — the same constructor already dereferences it unconditionally earlier (:133, :101 via `RenderTargetTextures::Create`), and boot order guarantees non-null; keep the comment [~5m]
- Delete the dead sampler filter "restore" (:418-420) — the struct is LINEAR from :394-395 and unmodified between; residue from a prior version [~5m]
- `CHECK_VK`-wrap the bare `vkBeginCommandBuffer`/`vkEndCommandBuffer` on the acquire CB (:558,611) — the only bare begin/end pair in the batch [~5m]
- Remove the pointless `bNeedsAcquire` alias of the unmodified parameter (:571) [~5m]
- Drop the two redundant explicit `Destroy()` calls before `Create` in `BlurLightingTexture` (:720-724, :742-746) — `Texture::Create` self-destroys (`Objects/Texture.cpp:162-164`); same family as ReviewSweepQuickWins item 12 but that plan covers `PipelineManager.cpp` only [~5m]

### Engine/Source/Graphics/Managers/TextureUploadManager.cpp
- Extract `ResetUploadProgress()` for the five-field in-progress reset hand-copied at :83-87, :322-326, :574-578 (header groups the fields as "In-progress upload state", `.h:61-66`); leave the deliberate partial reset in the DeviceLost catch (:297-303) [~15m]
- One-line comment on the deliberately unchecked `vkWaitForFences` in `DestroyTransferResources` (:97) — the file's only unchecked Vulkan result; prevent a future "fix" [~5m]

### Engine/Source/Graphics/Managers/TextureCache.{h,cpp}
- Shrink `TryLoadCachedTexture`'s 8 parameters (`TextureCache.h:28`, `.cpp:203`) — four duplicate what `rTexture.mInfo` already holds at the single caller (`GeneratePbrLutBrdf`) [~15m]

### Engine/Source/Graphics/Managers/ImGuiManager.cpp
- ASSERT/log the discarded `bool` results of `ImGui_ImplWin32_Init` (:115) and `ImGui_ImplVulkan_Init` (:135) — opaque third-party boundary; boot-fail-loud [~5m]

### Engine/Source/Graphics/Managers/TextManager.{h,cpp}
- De-template `WriteQuads` (`TextManager.h:101-140` — `template<typename T, typename U>` with exactly one instantiation, sole caller `TextManager.cpp:96`); move the 40-line body to the .cpp. **Skip if `Engine/Architecture_LibraryReplacement.md` proceeds with the TextManager→imgui replacement (moot)** [~15m]

## Critical files
- `Engine/Source/Graphics/Managers/TextureManager.cpp`, `TextureUploadManager.{h,cpp}`, `TextureCache.{h,cpp}`, `ImGuiManager.cpp`, `TextManager.{h,cpp}`

## Out of scope
- The `WaitIdle` drain-probe handshake and upload-thread corruption catch (live plans: `TextureUploadTeardownRaces`, `CorruptTextureChunkLifecycleHardening`)
- Lazy-chunk CPU pool decommit (`TextureChunkCpuPoolReclaim`), bindless placeholder arrays (`Architecture_BindlessSlotLifecycle`)
- `CreateSamplers` table-driving (optional polish, not filed)

## Notes
- Invariant exposure: none — client/graphics-only; the validation item is trust-boundary hardening of on-disk data (soft-fail path mirrors the existing upload-thread behavior). `TextureManager.cpp`/`TextureUploadManager.cpp` are shared with four live plans (Order.md File Groups) — co-schedule or refresh citations
- Grill decision: none material; TextManager item is contingent on the library-replacement decision
