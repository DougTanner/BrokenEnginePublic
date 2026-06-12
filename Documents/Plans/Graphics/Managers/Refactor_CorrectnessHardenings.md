# Refactor: Correctness Hardenings (Graphics/Managers)

## Context

Source: /external-refactor-clean on `Engine/Source/Graphics/Managers` (non-recursive). Latent in-function
hazards: a stored dangling view, trust-boundary gaps at Vulkan API results, an allocation-tracker violation on
a reachable per-frame path, hazardous-but-currently-correct initializer syntax, and two cheap thread/content
hardenings. Each item is independent.

## Design

### Engine/Source/Graphics/Managers/PipelineManager.cpp — dangling pipeline-name `string_view`
- `CreateLightingPipelines` (`PipelineManager.cpp:158-161`) builds a loop-local
  `std::string strName = std::format("LightingSpread{}", iPass)` and passes it as `PipelineInfo::name`
  (a `std::string_view`, `Pipeline.h:87`); `Pipeline::Create` copies the info verbatim (`Pipeline.cpp:69`),
  so `mSpreadPipelines[iPass].mInfo.name` dangles for the pipeline's lifetime. Reachable read: the
  staleness-error LOGs format `pPipeline->mInfo.name` (`PipelineManager.cpp:850, 872`). Fix: static literal
  table (`kiMaxSpreadPasses` is compile-time) or an owning member array mirroring `mShadowPipelineNames`
  (`DynamicPipelines.cpp:86-87`, which exists precisely because names must outlive the create call). [~15m]

### Engine/Source/Graphics/Managers/InstanceManager.cpp — Vulkan trust-boundary gaps
- `SelectQueueFamilies`: the ASSERT at `:568` covers graphics+present only; `:571` then does
  `mVkQueueFamilyProperties.at(miTransferQueueFamilyIndex)`, which throws if no family advertised
  `VK_QUEUE_TRANSFER_BIT` (legal per spec — graphics/compute families implicitly support transfer without
  advertising it). Fall back to `miGraphicsQueueFamilyIndex` when unset and add transfer to the ASSERT.
  Root-CLAUDE.md trust boundary: OS/third-party API results. [~5m]
- `SelectPhysicalDevice`: `ASSERT(...maxPerStageResources > 200)` (`:425`) runs inside the adoption branch on
  *provisional* candidates — a weak iGPU enumerated before the winning dGPU trips it spuriously. Move the
  check after final selection (`:436`). [~5m]
- Ctor `layerSettings[7]` (`:138`) is filled via a manual counter (up to 2+5 entries) with no bound check;
  one more setting under `kbGpuAssistedValidation` silently overflows. `ASSERT(uiLayerSettingCount <=
  std::size(layerSettings))` after the constexpr blocks. [~5m]
- `DebugUtilsCallback` (`:63-64`): the Debug-Printf branch heap-allocates (`std::string` + `common::Split`
  returning `std::vector<std::string>`) per message — per-draw frequency in `kbDebugPrintf` builds, no
  suppression. Rewrite zero-alloc with `std::string_view::find_last_of('\n')` + `substr`. [~15m]

### Engine/Source/Graphics/Managers/TextureCache.cpp — unsuppressed screenshot readback allocation
- `CopyImageToHostMemory` (`TextureCache.cpp:33`) does `rOutData.resize(iTotalSize)` (plus staging-buffer
  creation) with no `ScopedSuppressAllocationTracking`/`// Heap:` — and it is main-loop-reachable per frame:
  `CommandBufferManager.cpp:177-184` (`kbScreenshots` trigger) → `SaveScreenshot` (`Screenshot.cpp:19-20`) →
  here, inside the submission path with tracking live. The buffer cannot use the workbuffer (it is `std::move`d
  into the async save lambda), so per root CLAUDE.md add the suppression + `// Heap:` comment at the top of
  the function. [~5m]

### Engine/Source/Graphics/Managers/SwapchainManager.cpp — positional-init masquerading as designated init
- `vkSubpassDescription` (`:80-89`) and `vkSubpassDependency` (`:104-110`) are aggregate-initialized with
  clauses of the form `obj.field = value,` — each clause is an *assignment expression whose value positionally
  initializes the next member* (the name is in scope inside its own initializer). Verified correct today
  (clause order matches member order exactly), but inserting/reordering one clause silently shifts every
  following value onto the wrong member while still compiling. Convert to designated initializers (`.flags =`)
  like the same struct types at `RenderTargetTexturesLighting.cpp:133-155`. Style rule 42. [~5m]
- Ctor `:365` resets `miImageAvailableIndex` after `mImageAvailableFences.resize` — copy-paste; the fence-ring
  cursor `miFenceAvailableIndex` (`SwapchainManager.h:35`, consumed at `:57-68`) was meant. Benign today
  (in-class initializers zero both on a fresh object) but documents the wrong invariant; fix the member name
  (or delete the redundant resets at `:309/:365/:379`). [~5m]

### Engine/Source/Graphics/Managers/TextureManager.cpp — lighting-blur slot registration check
- `kiLightingBlurSlots = 16` headroom (`TextureManager.cpp:273-274`) is guarded only by a generic index ASSERT
  at `TextureDescriptors.cpp:410` (loud crash, not silent — this codebase's ASSERT is active in all configs,
  `Common/ErrorUtils.h:12`). Add a one-shot `mLightingTextureCrcs.size() <= kiLightingBlurSlots` check inside
  `RegisterLightingTextureCrc` (`TextureManager.cpp:803-808`) naming the constant, so a 17th lighting texture
  fails at the cause. [~5m]

### Engine/Source/Graphics/Managers/TextManager — content hardening + thread tripwire
- `GetCharacter` (`TextManager.cpp:53-56`) indexes `mpCharactersEfigs[uiChar % 128]`: signed-`char` callers
  sign-extend UTF-8 bytes into arbitrary ASCII slots, and ctor-unfilled slots (control chars like `'\t'`) are
  nullptr, dereferenced unchecked by `WriteQuads` (`TextManager.h:152-158`). Audited: all current writers are
  engine/game-generated ASCII (player names / translated text go through ImGui), so unreachable today — one
  stray tab in a future profile string is a crash. Index with `unsigned char` semantics and fall back to a
  known-populated glyph (e.g. `'?'`) on a nullptr slot. Trust boundary: the .fnt pack content determines which
  slots are null. [~15m]
- `UpdateTextArea` (`TextManager.cpp:58`): add `ASSERT(common::gpMultithreading->IsMainThread())`
  (predicate exists, `Common/Threading/Multithreading.h:20`) — `gpTextAreas` is a header-inline mutable global
  writable from any TU; all current writers are main-thread (verified), the ASSERT keeps it that way. [~5m]

## Critical files
- `Engine/Source/Graphics/Managers/PipelineManager.cpp` (+ `.h` if a name array member lands)
- `Engine/Source/Graphics/Managers/InstanceManager.cpp`
- `Engine/Source/Graphics/Managers/TextureCache.cpp`
- `Engine/Source/Graphics/Managers/SwapchainManager.cpp`
- `Engine/Source/Graphics/Managers/TextureManager.cpp`
- `Engine/Source/Graphics/Managers/TextManager.cpp`

## Out of scope
- The TextureCache payload-size validation and the mislabeled "sourceCrc mismatch" LOG — already covered by
  `Graphics/TextureCacheValidatePayloadSize.md`.
- The skinning grow bugs — `Refactor_BufferGrowthAndBounds.md`.
- The `WaitIdle` TOCTOU and `CrcToIndex` synchronization — `Architecture_ThreadAndLifetimeGuards.md`.
- Function decompositions — the three `Refactor_*Decomposition` plans.

## Acceptance criteria
- Each hazard has its guard/fix landed; client builds clean; no behavior change on the happy path (the
  designated-init conversion and name-storage change are bit-identical).

## Notes
- No determinism/CRC exposure — all client render/boot paths. The `DebugUtilsCallback` rewrite is worth doing
  regardless of whether the validation layer delivers printf on a tracked thread (uncertainty noted by the
  review).

## Verification Notes

Verified against source 2026-06-11 (verification pass for the /external-deep-analysis run). All items
confirmed; no removals:

- **Dangling `string_view`**: loop-local `std::string strName` at `PipelineManager.cpp:158` passed as `.name`
  at `:161`; `PipelineInfo::name` is `std::string_view` (`Pipeline.h:87`); `Pipeline::Create` copies
  `mInfo = rInfo` (`Pipeline.cpp:69`); dangling reads in the staleness LOGs at `PipelineManager.cpp:850,872`
  (`rBinding.pPipeline->mInfo.name`). The `mShadowPipelineNames` interning precedent is at
  `DynamicPipelines.cpp:87`.
- **InstanceManager**: transfer family genuinely can stay `UINT32_MAX` (only set under `VK_QUEUE_TRANSFER_BIT`,
  `InstanceManager.cpp:538-549`); ASSERT at `:568` covers graphics+present only; `.at()` throw site at `:571`.
  Provisional-candidate ASSERT at `:425` inside the adoption branch (`:423-427`), final selection settles by
  `:434-436`. `layerSettings[7]` at `:138`. Debug-Printf branch heap-allocates at `:63-64`
  (`std::string` + `common::Split` → `std::vector<std::string>`).
- **Screenshot readback**: `rOutData.resize(iTotalSize)` at `TextureCache.cpp:33` with no suppression anywhere
  in the function; reachable via `CommandBufferManager.cpp:177-184` (`kbScreenshots`) → `Screenshot.cpp:19-20`;
  the vector is `std::move`d into the `std::async` lambda (`Screenshot.cpp:30`), so workbuffer is unusable.
- **SwapchainManager positional init**: `vkSubpassDescription` clauses at `:80-89` and `vkSubpassDependency`
  at `:104-110` are exactly the `obj.field = value,` form; clause order matches the Vulkan member order in both
  structs (correct today, fragile as described). Fence-cursor copy-paste confirmed: `:365` resets
  `miImageAvailableIndex` after `mImageAvailableFences.resize` (`:364`); the fence ring cursor is
  `miFenceAvailableIndex` (`SwapchainManager.h:35`, consumed `:57-68`); the other resets are `:309`
  (`miFramebufferIndex`) and `:379` (`miImageAvailableIndex` after the semaphore resize — that one is the right
  member). All three redundant on a fresh object (in-class initializers; SwapchainManager is reconstructed on
  recreate).
- **TextureManager / TextManager**: `kiLightingBlurSlots = 16` at `TextureManager.cpp:273-274`; generic ASSERT
  at `TextureDescriptors.cpp:410`; `RegisterLightingTextureCrc` at `TextureManager.cpp:803-808`. `GetCharacter`
  at `TextManager.cpp:53-56` (`mpCharactersEfigs[uiChar % 128]`, ctor fills only font-present IDs < 128 —
  control-char slots stay nullptr); `WriteQuads` derefs `pCharacter` unchecked at `TextManager.h:152-158`.
  `UpdateTextArea` at `TextManager.cpp:58`; `gpTextAreas` is a header-inline mutable global
  (`TextManager.h:34`); `IsMainThread` exists at `Common/Threading/Multithreading.h:20`. ASSERT active in all
  configs confirmed (`Common/ErrorUtils.h:12`).
- Out-of-scope cross-references verified: `Graphics/TextureCacheValidatePayloadSize.md` covers
  `TryLoadCachedTexture` (different function from `CopyImageToHostMemory` — no overlap).
