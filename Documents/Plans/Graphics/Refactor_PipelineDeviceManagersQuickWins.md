# Refactor: Pipeline/Device Managers Quick Wins

## Context
Source: /external-refactor-clean on Engine/Source (recursive). Mechanical in-function cleanup across the pipeline/command-buffer/device manager batch. Hot-path allocation, DirectXMath, and guard-scope sweeps came back clean; these are the residual items. One item needs a decision (ASSERT-vs-fallback contradiction).

## Design

### Engine/Source/Graphics/Managers/InstanceManager.{h,cpp}
- Delete the dead `mbFoundKhronosValidation` member (`InstanceManager.h:60`, written once at `InstanceManager.cpp:746`, never read — repo-wide grep) [~5m]
- Merge the two `if constexpr (kbShaderRealtimeClock)` blocks in `ValidatePhysicalDeviceCapabilities` (`InstanceManager.cpp:509-518`, split only by an unrelated ASSERT) [~5m]
- Inline the two-line `SelectPhysicalDevice()` wrapper (`InstanceManager.cpp:395-399`, decl `InstanceManager.h:68`) at its single ctor call site [~5m]

### Engine/Source/Graphics/Managers/DeviceManager.cpp
- `CHECK_VK`-wrap the bare `vkEnumerateDeviceExtensionProperties` calls (:18,20) and the two `vkGetPipelineCacheData` calls in the dtor (:321,323) — every comparable enumeration in the directory is wrapped [~5m]
- Resolve the separate-present-queue contradiction: `DeviceManager.cpp:246` does `ASSERT(false)` then executes a correct fallback, while `SwapchainManager::CreateSwapchain` (:239-257) carries live `VK_SHARING_MODE_CONCURRENT` support for exactly that case. Either the configuration is supported (delete the ASSERT) or not (delete the CONCURRENT machinery, fail loud at device selection) — today the debug build breaks on hardware the release build handles [~15m]

### Engine/Source/Graphics/Managers/SwapchainManager.cpp
- Replace the two width/height if/else-if clamp ladders in `CreateSwapchain` (:196-214) with `std::clamp` [~5m]

### Engine/Source/Graphics/Managers/CommandBufferManager.{h,cpp}
- Delete the `bSignalFence` parameter (`CommandBufferManager.h:17,35`, `.cpp:139,179,190`) — `false` at the single caller (`Graphics.cpp:252`); the true-branch is dead and the block comment (:173-177) already documents "which is ALWAYS". Submit with `VK_NULL_HANDLE`, keep the reset/UI-signal pairing comment [~15m]
- Move `mbSaveScreenshot` (`CommandBufferManager.h:30`) to `Graphics` — CommandBufferManager never touches it; it's a mailbox between `game::Game` (`Game.cpp:663`) and `Graphics.cpp:263-265` [~15m]

### Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp / CommandBufferRecordGlobal.cpp
- Drop the redundant `static_cast<uint32_t>` on the already-`uint32_t` extent members (`CommandBufferRecordMain.cpp:84`) [~5m]
- Remove the vestigial `kGpuTimerTerrainGen` start/stop (`CommandBufferRecordGlobal.cpp:119-127`) — it wraps exactly one inner timer pair (`kGpuTimerTerrainElevation`); also delete its entries in the engine `GpuTimers` enum and `kGpuTimerNames` table (`Profile/ProfileManagerBase.h:146,186` — the `static_assert` keeps them in sync) and de-indent the inner entry [~15m]

### Engine/Source/Graphics/Managers/BufferManager.{h,cpp}
- Replace `std::array<VisibleAreaMeshLod, ...> mWaterMeshLods` (`BufferManager.h:108`) and the `std::array&` parameter of `BuildLodConcatMesh` (`BufferManager.cpp:723-725`) with a C array + `kiVisibleAreaLodCount` per style rule 21; fix the adjacent `int` → `int64_t` drift (`BufferManager.h:99`, `.cpp:729,742`) [~15m]
- `CreateVisibleAreaMesh` (:685) direct-write: change its vector-ref params to raw pointers and delete the per-LOD temp vectors + memcpys in `BuildLodConcatMesh` (:749-754); boot/recreate-time only [~15m]

### Engine/Source/Graphics/Managers/DynamicPipelines.{h,cpp}
- `CreateDepositPipeline` takes 9 params (`DynamicPipelines.h:71`, `.cpp:266`) and passes `DescriptorInfo` by value — introduce a `DepositPipelineDesc` struct or at minimum `const DescriptorInfo&` [~15m]

### Engine/Source/Graphics/Managers/PipelineManager.cpp
- Delete the inner `using enum` pair in `CreateDebugRenderPipelines` (:761-762) — duplicates the namespace-scope declarations at :11-12 [~5m]

### Optional (include only if time allows)
- Decompose the ~305-line `DeviceManager` constructor (:8-313) into phase helpers (`BuildDeviceExtensions`, `CreateLogicalDevice`, `CreateDescriptorPool`); boot-time readability only [~1h]

## Critical files
- `Engine/Source/Graphics/Managers/`: InstanceManager, DeviceManager, SwapchainManager, CommandBufferManager, CommandBufferRecordMain, CommandBufferRecordGlobal, BufferManager, DynamicPipelines, PipelineManager (one line)
- `Engine/Source/Graphics/Graphics.{h,cpp}` (`mbSaveScreenshot` new home; `bSignalFence` caller)
- `Projects/BrokenEngineSandbox/Source/Game.cpp` (`mbSaveScreenshot` toggle site)
- `Engine/Source/Profile/ProfileManagerBase.h` (`GpuTimers` enum + `kGpuTimerNames` table, terrain-gen timer removal)

## Out of scope
- `PipelineManager.cpp` split / redundant `Destroy()`s / descriptor-generation verifier (live plans: `Refactor_PipelineManagerSplit`, `Meta/ReviewSweepQuickWins` item 12, `Architecture_BindlessSlotLifecycle`)
- `CommandBufferRecordMain::Record` decomposition and shared record helpers (`Graphics/Refactor_CommandBufferRecordDedup.md`)
- `GrowMeshDataBuffer`/`GrowJointMatrixBuffer` fold and `CreateDebugMeshBuffers` table-drive (optional, deliberately not filed — stable code with load-bearing comments)

## Notes
- Invariant exposure: none — client/graphics-only, boot/teardown or record-once paths; no determinism/CRC/wire. `PipelineManager.cpp` line cites drift if `Refactor_PipelineManagerSplit` lands first — refresh
- Grill decision: item 5 — support graphics≠present queues (delete ASSERT) vs fail loud (delete CONCURRENT machinery); recommend delete the ASSERT (the fallback is correct and tested by the release build)

## Verification Notes

- All items re-verified against source 2026-07-02: `mbFoundKhronosValidation` write-only (repo grep), `bSignalFence` false at its single caller (`Graphics.cpp:252`), `mbSaveScreenshot` untouched by CommandBufferManager itself (only `Game.cpp:663` writes, `Graphics.cpp:263-265` consumes), the `DeviceManager.cpp:246` `ASSERT(false)` sits ahead of a complete fallback (queue is created — the ctor's unique-family loop includes the present family — and `SwapchainManager` carries live `VK_SHARING_MODE_CONCURRENT` support at `:239-257`).
- The `using enum` dedup (`PipelineManager.cpp:761-762`) becomes moot if `Refactor_PipelineManagerSplit` lands first: `CreateDebugRenderPipelines` moves to the new `PipelineManagerEffects.cpp`, where a local `using enum` pair is needed again. Drop the item in that ordering.
