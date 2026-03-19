# Plan Execution Order

Score = Effort - Impact + Risks (lower = higher priority)

| # | Plan | Effort | Impact | Risks | Score | Items | Notes |
|---|------|--------|--------|-------|-------|-------|-------|
| 1 | `Game/Network/TechDebt_DeadCode.md` | 1 | 2 | 1 | 0 | 2 | Remove unused rAlignments and rReconcileContext parameters |
| 2 | `Game/Ui/TechDebt_DeadCode.md` | 1 | 2 | 1 | 0 | 1 | Remove empty Wrapper.h (no game-specific extensions) |
| 3 | `Game/Ui/Screens/TechDebt_Duplication.md` | 2 | 3 | 1 | 0 | 1 | Consolidate RenderShieldBar/RenderArmorBar into parameterized RenderBar() |
| 4 | `Game/Network/TechDebt_Duplication.md` | 2 | 4 | 2 | 0 | 1 | Extract RestoreUnconsumedServerData helper (correctness-critical sync) |
| 5 | `Game/TechDebt_Simplifications.md` | 2 | 3 | 1 | 0 | 2 | Convert music playlists to constexpr arrays; guard sound settings with BT_CLIENT |
| 6 | `Game/TechDebt_Duplication.md` | 2 | 3 | 1 | 0 | 2 | Extract frame init helper; consolidate music start pattern |
| 7 | `Game/Architecture_ProcessMenuInput.md` | 2 | 3 | 1 | 0 | 1 | Extract ProcessDebugInput() from 127-line ProcessMenuInput() |
| 8 | `Common/TechDebt_DeadCode.md` | 2 | 3 | 1 | 0 | 8 | Remove 8 unused functions |
| 9 | `Graphics/Render/TechDebt_DeadCode.md` | 2 | 3 | 1 | 0 | 7 | Remove unused includes, dead static, noop lerps/pow, make function file-static |
| 10 | `Graphics/Objects/TechDebt_DeadCode.md` | 2 | 3 | 1 | 0 | 7 | Remove unused vkRenderPass param, kSamplerNearestBorder flag + sampler |
| 11 | `Network/Client/TechDebt_DeadCode.md` | 1 | 2 | 1 | 0 | 1 | Remove unused MemoryManager.h include from ClientReceive.cpp |
| 12 | `Network/Server/TechDebt_DeadCode.md` | 1 | 2 | 1 | 0 | 4 | Remove dead pcAddress resolution, unused NetworkCursor.h includes |
| 13 | `Network/Client/Architecture_ClientGuard.md` | 1 | 2 | 1 | 0 | 1 | Add `#ifdef BT_CLIENT` guard to Client.h for consistency |
| 14 | `Network/Client/TechDebt_Duplication.md` | 2 | 3 | 1 | 0 | 2 | Deduplicate SendUnsubscribe + extract cancelled subscription helper |
| 15 | `Server/TechDebt_Duplication.md` | 2 | 3 | 1 | 0 | 2 | Extract snprintf+TextOutA lambda, extract PaintGridMap() from 252-line function |
| 16 | `DataPacker/TechDebt_DeadCode.md` | 1 | 2 | 1 | 0 | 4 | Remove unused kiDataPackerVersion, sbSingleThread, miChannels |
| 17 | `ExportJobs/TechDebt_DeadCode.md` | 2 | 3 | 1 | 0 | 4 | Remove dead AncestorJointResult/FindNearestAncestorJoint, unused iAnimIndex, scope gGltfContext |
| 18 | `Engine/TechDebt_DeadCode.md` | 1 | 2 | 1 | 0 | 5 | Remove unused RawInput fwd decl, dead TickFramesAndRender, unused include, localize static |
| 19 | `Debug/TechDebt_DeadCode.md` | 1 | 2 | 1 | 0 | 4 | Remove dead VkDebugReportFlagsEXT map/branch, duplicate key, sentinel entry |
| 20 | `Frame/Collections/Pushers/TechDebt_Optimizations.md` | 2 | 3 | 1 | 0 | 5 | Reduce zone memory 4x (int64→int16), memcpy Update loop, #ifdef BT_CLIENT render guards |
| 21 | `Frame/Collections/AreaLights/TechDebt_RenderOptimization.md` | 1 | 2 | 1 | 0 | 1 | Cache duplicate CrcToIndex lookup in render loop |
| 22 | `Frame/Collections/PointLights/TechDebt_Duplication.md` | 1 | 2 | 1 | 0 | 1 | Cache duplicate CrcToIndex lookup in render loop |
| 23 | `Frame/Collections/Puffs/TechDebt_DeadCode.md` | 1 | 2 | 1 | 0 | 1 | Remove dead defensive guard in Destroy(); non-controlled puffs cannot exist |
| 24 | `Frame/Collections/WindTrails/TechDebt_Duplication.md` | 2 | 3 | 1 | 0 | 2 | Extract shared EraseStaleRenderState template from WindTrails/SmokeTrails |
| 25 | `File/TechDebt_DeadCode.md` | 1 | 2 | 1 | 0 | 2 | Remove unused GetFileSize() and LazyChunk::eDataType |
| 26 | `Graphics/Managers/Architecture_MissingInclude.md` | 1 | 2 | 1 | 0 | 1 | Add missing direct TweaksScreen.h include in ImGuiManager.h |
| 27 | `Graphics/Managers/Architecture_BufferManagerEnvy.md` | 2 | 4 | 2 | 0 | 1 | Refactor BufferManager→DynamicPipelines descriptor update coupling |
| 28 | `ExportJobs/Architecture_ExceptionHandling.md` | 1 | 2 | 1 | 0 | 3 | Add missing `virtual` keywords to ExportAudio.h |
| 29 | `DataPacker/TechDebt_Simplifications.md` | 2 | 3 | 1 | 0 | 4 | Make 5 Texture methods static, remove unused param, optimize Downsize erase |
| 30 | `Frame/TechDebt_DeadCode.md` | 1 | 2 | 1 | 0 | 2 | Remove unused HealthDamage.h and Players.h includes |
| 31 | `Frame/TechDebt_Duplication.md` | 1 | 2 | 1 | 0 | 1 | Extract duplicated binary search comparator in Alignments.cpp |
| 32 | `Frame/Collections/Billboards/Architecture_RecordingBranch.md` | 1 | 2 | 1 | 0 | 1 | Remove inconsistent kbEnableRecording branch in EndRender (superseded if collection removed) |
| 33 | `Game/Input/TechDebt_Simplifications.md` | 2 | 3 | 2 | 1 | 2 | Replace static mouse position with member; fix non-idiomatic operator== |
| 34 | `Game/Input/Architecture_ServerInputCrc.md` | 5 | 7 | 3 | 1 | 3 | SharedMembers() tuple for TransferData; prevents silent desync drift |
| 35 | `Game/Network/Architecture_ReconcileRefactor.md` | 2 | 3 | 2 | 1 | 1 | Extract FindMatchingPlayerInCoord from 6-level nesting |
| 36 | `Common/TechDebt_DeprecatedAPIs.md` | 3 | 6 | 4 | 1 | 2-4 | Replace std::wstring_convert before C++26 removes it |
| 37 | `Ui/Screens/TweaksScreen/TechDebt_Duplication.md` | 2 | 3 | 2 | 1 | 14 | Extract radio button helper + standardize kiSection pattern |
| 38 | `Network/Client/TechDebt_Simplifications.md` | 2 | 2 | 1 | 1 | 3 | Review 15 DT TEMP diagnostic logs for removal/promotion |
| 39 | `Graphics/Render/TechDebt_Duplication.md` | 2 | 3 | 2 | 1 | 1 | Extract shared spread quad offset helper from smoke/wind uniforms |
| 40 | `Graphics/Objects/TechDebt_Duplication.md` | 3 | 4 | 2 | 1 | 5 | std::exchange Buffer moves, push constants helper, descriptor binding helper, image view helper, depth bias constants |
| 41 | `Graphics/Managers/TechDebt_Duplication.md` | 3 | 4 | 2 | 1 | 5 | Deduplicate smoke/wind pipelines, buffer init, sampler creation, shadow blur |
| 42 | `Audio/TechDebt_DeadCode.md` | 2 | 2 | 1 | 1 | 4 | Remove unused LOG_STATIC_VOICES macro and uiCrc parameter |
| 43 | `Frame/Collections/SmokeTrails/TechDebt_DeadCode.md` | 2 | 2 | 1 | 1 | 3 | Consolidate SmokeTrailsUpdate.cpp into SmokeTrails.cpp; remove unnecessary file |
| 44 | `Frame/Collections/SmokeTrails/Architecture_RenderSplit.md` | 2 | 2 | 1 | 1 | 1 | Extract geometry helper from 113-line Render(); high param count limits benefit |
| 45 | `Common/TechDebt_Duplication.md` | 3 | 4 | 2 | 1 | 3 | Extract xorshift helper, deduplicate ColorToVector, extract magic number |
| 46 | `Audio/TechDebt_Duplication.md` | 3 | 4 | 2 | 1 | 2 | Extract voice cleanup helper, extract music stream creation helper |
| 47 | `File/TechDebt_Duplication.md` | 3 | 4 | 2 | 1 | 3 | Consolidate 4 memory stat methods into 2 returning MemoryStats |
| 48 | `Audio/Architecture_UpdateSplit.md` | 4 | 5 | 2 | 1 | 3 | Split 217-line Update() into focused subfunctions |
| 49 | `Server/Architecture_Decoupling.md` | 3 | 3 | 2 | 2 | 3 | TotalEntityCount() accessor on FrameInterpolate, pre-compute cell lookups |
| 50 | `Graphics/Render/Architecture_GlobalUniformsSplit.md` | 4 | 4 | 2 | 2 | 5 | Split 310-line RenderFrameGlobal into 4 helpers + remove unused param |
| 51 | `Graphics/Objects/Refactor_Simplifications.md` | 3 | 3 | 2 | 2 | 5 | Extract CreateSingleSetPipelineLayout, hoist indirect count, RecordBeginRenderPass bool→flags |
| 52 | `Frame/Collections/WindTrails/Refactor_Simplifications.md` | 2 | 2 | 2 | 2 | 1 | Extract BuildWindTrailQuad helper from 97-line Render() |
| 53 | `Common/Refactor_Simplifications.md` | 3 | 3 | 2 | 2 | 4 | ScopedLambda modernization, PathToCppVariable single-pass, dead InsideAreaVertices |
| 54 | `Ui/Screens/TweaksScreen/Architecture_SliderMapExtract.md` | 3 | 3 | 2 | 2 | 4 | Extract 252-line GetSliderMap() to TweaksScreenSliderMap.cpp |
| 55 | `DataPacker/TechDebt_Duplication.md` | 3 | 3 | 2 | 2 | 1 | Extract write-if-changed helper for generated headers |
| 56 | `Game/Network/Architecture_FileSplit.md` | 5 | 4 | 2 | 3 | 2 | Split ReconcileReplay.cpp (682 lines) into 3 + ServerSession.cpp (636 lines) into 5 companion files |
| 57 | `Frame/Collections/SmokeTrails/TechDebt_Duplication.md` | 5 | 4 | 2 | 3 | 2 | BeginRender cleanup template + AllocateAndCopy CopyMembers utility (cross-collection) |
| 58 | `Graphics/Objects/Architecture_OversizedFunctions.md` | 5 | 5 | 3 | 3 | 7 | Split CreateGraphicsPipeline (473→helpers), Write (231→helpers), Create (262→helpers), table-driven TransitionImageLayout |
| 59 | `Common/Architecture_UtilsCohesion.md` | 4 | 3 | 3 | 4 | 3 | Move color functions from Utils to MathUtils |
| 60 | `Graphics/Managers/Architecture_FileSplits.md` | 5 | 4 | 3 | 4 | 5 | Split CommandBufferManager/PipelineManager/BufferManager/InstanceManager/RenderTargetTextures |
| 61 | `ExportJobs/TechDebt_Duplication.md` | 5 | 4 | 3 | 4 | 6 | 6 duplication instances across skeleton/scene/texture code |
| 62 | `ExportJobs/Architecture_UtilsCohesion.md` | 4 | 3 | 3 | 4 | 3 | Move shader-specific CheckDirty logic from ExportJob base to ExportShader |
| 63 | `File/Architecture_FileSplit.md` | 5 | 4 | 3 | 4 | 4 | Split 685-line FileManager.cpp into 4 files + vcxproj updates |
| 64 | `Common/Architecture_ExceptionHandling.md` | 4 | 2 | 3 | 5 | 1 | Extract SetupExceptionHandling to separate .cpp file + vcxproj |
| 65 | `ExportJobs/Refactor_Simplifications.md` | 5 | 4 | 4 | 5 | 6 | Consolidate BuildNodeSkeleton/LoadSkeleton, extract helpers, fix typo |

## Dependencies

Plans that must be executed in order due to shared files or stale line numbers:

- `Common/TechDebt_DeadCode` → `Common/Architecture_UtilsCohesion` — DeadCode removes GetStringValueFromHKLM; UtilsCohesion moves from same files
- `Common/TechDebt_DeadCode` → `Common/TechDebt_DeprecatedAPIs` — DeadCode removes ToU32string; may eliminate need for char32_t codecvt migration
- `Common/TechDebt_Duplication` ↔ `Common/Architecture_UtilsCohesion` — Both modify ColorToVector declarations; execute together
- `DataPacker/TechDebt_DeadCode` → `DataPacker/TechDebt_Simplifications` — Both modify Texture.h
- `ExportJobs/TechDebt_DeadCode` → `ExportJobs/Refactor_Simplifications` — Both modify ExportSceneVertices
- `ExportJobs/Architecture_UtilsCohesion` → `ExportJobs/Refactor_Simplifications` — UtilsCohesion removes code from ExportJob.cpp first
- `Audio/TechDebt_DeadCode` → `Audio/TechDebt_Duplication` → `Audio/Architecture_UpdateSplit` — Sequential within AudioManager.cpp
- `File/TechDebt_DeadCode` → `File/TechDebt_Duplication` → `File/Architecture_FileSplit` — Sequential within FileManager.h/cpp
- `Graphics/Render/TechDebt_DeadCode` → `Graphics/Render/TechDebt_Duplication` → `Graphics/Render/Architecture_GlobalUniformsSplit` — Sequential; line numbers shift after dead code removal
- `Frame/Collections/Billboards/Architecture_RecordingBranch` — No longer depends on TechDebt_DeadCode (stale comments already removed)
- `Frame/Collections/SmokeTrails/TechDebt_DeadCode` → `Frame/Collections/SmokeTrails/Architecture_RenderSplit` — DeadCode merges Update into core file; do in sequence for clean diffs
- `Frame/Collections/WindTrails/TechDebt_Duplication` ↔ `Frame/Collections/SmokeTrails/TechDebt_Duplication` — Both cover shared BeginRender cleanup extraction; implement together
- `Graphics/Objects/TechDebt_DeadCode` → `Graphics/Objects/TechDebt_Duplication` → `Graphics/Objects/Refactor_Simplifications` → `Graphics/Objects/Architecture_OversizedFunctions` — Sequential within Pipeline/Texture files; line numbers shift after each phase
- `Graphics/Managers/TechDebt_Duplication` → `Graphics/Managers/Architecture_FileSplits` — Dedup helpers in BufferManager define natural split boundaries; do duplication removal first
- `Graphics/Managers/Architecture_BufferManagerEnvy` → `Graphics/Managers/Architecture_FileSplits` — Envy refactoring changes BufferManager internals; do before splitting
- `Network/Client/TechDebt_DeadCode` → `Network/Client/TechDebt_Duplication` — Both modify ClientReceive.cpp; line numbers shift after include removal
- `Server/TechDebt_Duplication` → `Server/Architecture_Decoupling` — Both modify ServerDisplay.cpp; line numbers shift after function extraction
- `Game/TechDebt_Simplifications` → `Game/TechDebt_Duplication` — Both modify Game.h music playlist members; simplifications changes type first
- `Game/TechDebt_Duplication` → `Game/Architecture_ProcessMenuInput` — Both modify Game.cpp; line numbers shift after helper extraction
- `Game/Network/TechDebt_DeadCode` → `Game/Network/Architecture_ReconcileRefactor` → `Game/Network/Architecture_FileSplit` — Sequential within ReconcileReplay.cpp; dead code first, then refactor, then split
- `Game/Network/TechDebt_Duplication` → `Game/Network/Architecture_FileSplit` — Duplication fix modifies ClientReconciler.cpp before file split
- `Game/Input/TechDebt_Simplifications` → `Game/Input/Architecture_ServerInputCrc` — Both modify Input.cpp; simplifications first for clean line numbers
- `Ui/Screens/TweaksScreen/TechDebt_Duplication` → `Ui/Screens/TweaksScreen/Architecture_SliderMapExtract` — Duplication adds helper to .h/.cpp; SliderMapExtract moves code from .cpp. Do duplication first to avoid line number conflicts

## File Groups

Plans that touch the same files and should be done in a single session:

- **Utils.h/cpp + MathUtils.h/cpp + Random.h/cpp**: `Common/TechDebt_DeadCode`, `Common/TechDebt_Duplication`, `Common/Refactor_Simplifications`, `Common/Architecture_UtilsCohesion`
- **Texture.h/cpp + DataPacker Main.cpp**: `DataPacker/TechDebt_DeadCode`, `DataPacker/TechDebt_Simplifications`, `DataPacker/TechDebt_Duplication`
- **ExportScene*.cpp + ExportJob.cpp + ExportShader.h/cpp**: `ExportJobs/TechDebt_DeadCode`, `ExportJobs/TechDebt_Duplication`, `ExportJobs/Architecture_UtilsCohesion`, `ExportJobs/Refactor_Simplifications`, `ExportJobs/Architecture_ExceptionHandling`
- **EnumToString.h + Engine.h**: `Debug/TechDebt_DeadCode`
- **AudioManager.h/cpp + StaticVoice.h/cpp + StreamingVoice.cpp**: `Audio/TechDebt_DeadCode`, `Audio/TechDebt_Duplication`, `Audio/Architecture_UpdateSplit`
- **FileManager.h/cpp**: `File/TechDebt_DeadCode`, `File/TechDebt_Duplication`, `File/Architecture_FileSplit`
- **GameBase.h/cpp + Main.cpp + CrashReport.cpp**: `Engine/TechDebt_DeadCode`
- **Alignments.cpp + Collision.cpp + FrameBase.cpp**: `Frame/TechDebt_DeadCode`, `Frame/TechDebt_Duplication`
- **Billboards/*.cpp**: `Frame/Collections/Billboards/Architecture_RecordingBranch`
- **SmokeTrails/*.cpp + WindTrails/*.cpp + CollectionMemory.h**: `Frame/Collections/SmokeTrails/TechDebt_DeadCode`, `Frame/Collections/SmokeTrails/TechDebt_Duplication`, `Frame/Collections/SmokeTrails/Architecture_RenderSplit`, `Frame/Collections/WindTrails/TechDebt_Duplication`, `Frame/Collections/WindTrails/Refactor_Simplifications`
- **Pipeline.h/cpp + PipelineCreator.cpp + PipelineDescriptorWriter.cpp + Texture.h/cpp + Buffer.cpp + TextureManager.h/cpp + CommandBufferManager.cpp**: `Graphics/Objects/TechDebt_DeadCode`, `Graphics/Objects/TechDebt_Duplication`, `Graphics/Objects/Refactor_Simplifications`, `Graphics/Objects/Architecture_OversizedFunctions`
- **Render/*.cpp + Render.h**: `Graphics/Render/TechDebt_DeadCode`, `Graphics/Render/TechDebt_Duplication`, `Graphics/Render/Architecture_GlobalUniformsSplit`
- **BufferManager.cpp + DynamicPipelines.cpp + PipelineManager.cpp + CommandBufferManager.cpp + InstanceManager.cpp + RenderTargetTextures.cpp + ImGuiManager.h**: `Graphics/Managers/TechDebt_Duplication`, `Graphics/Managers/Architecture_BufferManagerEnvy`, `Graphics/Managers/Architecture_FileSplits`, `Graphics/Managers/Architecture_MissingInclude`
- **Client.h + ClientReceive.cpp + ClientSend.cpp + ClientSessionBase.cpp**: `Network/Client/TechDebt_DeadCode`, `Network/Client/TechDebt_Duplication`, `Network/Client/TechDebt_Simplifications`, `Network/Client/Architecture_ClientGuard`
- **Server.cpp + ServerSend.cpp**: `Network/Server/TechDebt_DeadCode`
- **ServerDisplay.cpp + Frame/Frame.h**: `Server/TechDebt_Duplication`, `Server/Architecture_Decoupling`
- **Game.h + Game.cpp**: `Game/TechDebt_Simplifications`, `Game/TechDebt_Duplication`, `Game/Architecture_ProcessMenuInput`
- **ClientReconciler.cpp + ReconcileReplay.cpp + ReconcileReplay.h**: `Game/Network/TechDebt_DeadCode`, `Game/Network/TechDebt_Duplication`, `Game/Network/Architecture_ReconcileRefactor`, `Game/Network/Architecture_FileSplit`
- **Input.cpp + Input.h + StatusChange.h**: `Game/Input/TechDebt_Simplifications`, `Game/Input/Architecture_ServerInputCrc`
- **MenuUtils.h/cpp + HudScreen.cpp**: `Game/Ui/Screens/TechDebt_Duplication`
- **TweaksScreen.h/cpp + TweaksScreenSmoke.cpp + TweaksScreenWaterLow.cpp + TweaksScreenWaterMedium.cpp + all section files**: `Ui/Screens/TweaksScreen/TechDebt_Duplication`, `Ui/Screens/TweaksScreen/Architecture_SliderMapExtract`
