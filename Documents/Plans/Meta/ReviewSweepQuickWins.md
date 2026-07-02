# Review-Sweep Quick Wins Batch

## Context

A full post-implementation review of the plan-execution range 9c0b44d8..672b292e surfaced a batch of small, independent fixes — each real, none big enough for its own plan. One mechanical session; the only judgment call is item 7's one-liner.

## Design (checklist — each item is independent)

1. `Projects/BrokenEngineSandbox/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.h:43` `TweakSectionFlags` — `static_assert(kCount <= 32)` permits exactly 32, but `kAllSectionFlags` computes `(1u << kCount) - 1u`, UB at that bound. Tighten to `< 32` (or compute the mask overflow-safely). Latent only — `kCount` is 12.
2. `Engine/Source/Ui/CurveWidget.cpp:90` — hit-target regression vs the removed hand-rolled editor: ImPlot `DragPoint`'s grab half-size is ~5 px (`kfPointRadius`) where the old `FindPointAt` used 24 px, so a left-click 10–24 px from a control point now *adds* a point instead of grabbing; a freshly added point also no longer auto-enters drag (old code set the drag index). Pass a larger radius to `DragPoint` and/or pre-check proximity via `GetPlotMousePos` before `AddPoint`; restore add-then-drag. Debug-tool UX only.
3. Dead `fTerrainEarlyOut` uniform — the C++ still uploads it (`Engine/Source/Graphics/Render/GlobalUniforms.cpp:394`) and the `gTerrainEarlyOut` wrapper exists (`WrapperBase.cpp:14`), but no shader reads the field (Terrain.frag's early-out discard was removed this range; `fWaterEarlyOut` by contrast is still read). Drop the `GlobalLayout` field, the wrapper, and the upload — compile-checked on both language sides.
4. `DataPacker/Source/ExportJobs/Island/BakeRoute.cpp:190` `RemoveOrphanedLeafFolders` — `std::stoll` on an all-digit directory name throws `std::out_of_range` for ≥ 19 digits (disk contents are a trust boundary; the sweep filters non-numeric names but not over-long ones). Length-cap the digit test or catch-and-skip.
5. `BrokenEngineSandbox.vcxproj` + `BrokenEngineSandboxServer.vcxproj` (3 configs each) — `AdditionalIncludeDirectories` entry `$(ProjectDir)\..\..\..\Data` resolves to `Projects\Data`, which does not exist; the old literal duplicate `..\..\Data` entry was "deduped" by rewriting instead of deleting. Delete all 6 occurrences.
6. `Engine/Source/Input/RawInputManager.cpp:54,76` `UpdateFocus` — the failure logs call `LastErrorString()`, which now returns an owned `std::string` (thread-safety fix this range) that always exceeds SSO → heap allocation in the tracked main loop on a *recoverable* path. Wrap with `ScopedSuppressAllocationTracking` + `// Heap:` comment.
7. `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/SpaceshipsRender.cpp:77` — the `ASSERT(!sbRenderActive.exchange(true))` tripwire has no unwind protection: a throw before the trailing `store(false)` (`:189`) wedges it true and every later `Render` false-asserts. Make it RAII. Decide inline: promote the tripwire to the other slab-cursor collections or comment why it is Spaceships-only.
8. `DataPacker/Source/ExportJobs/Texture/Texture.cpp:117` `LoadExr` (+ the gli/KTX `.string()` call sites in `ExportTexture`) — pass `u8string()` at every third-party `const char*` path boundary, as the stb sites now do; non-ASCII asset paths currently fail for EXR/KTX only.
9. `DataPacker/Source/ExportJobs/ExportShader.h:44` `RunVulkanTool` — takes `std::wstring& rParameters` but never mutates it; make it `const`.
10. `Engine/Source/Graphics/Objects/Pipeline.h:166` `miIndirectSlotCount` — comment says "= max(framebufferCount, 3)" but the `kIndirectDeviceLocal` compute branch (`PipelineCreator.cpp:788`) sets 1, and `SetupIndirectBuffer` stamps it even with no indirect flag. Fix the comment.
11. `Engine/Source/Graphics/Objects/Pipeline.cpp:290` `RecordDrawIndirectSet2` — add the `ASSERT(iIndirectSlot < miIndirectSlotCount)` bounds check its siblings `RecordDrawIndirect`/`RecordComputeIndirect` gained this range.
12. `Engine/Source/Graphics/Managers/PipelineManager.cpp:182,215,237` — explicit `.Destroy()` immediately before `Create` is redundant (`Pipeline::Create` destroys first; the `Create*` members are ctor-only). Delete the three lines.
13. `Engine/Source/Graphics/Managers/DynamicPipelines.cpp:78-90,130-142` — the two verbatim `try { CreateModelPipeline(...) } catch (CorruptStreamException) { LOG; throw; }` blocks: fold the log-and-rethrow into `CreateModelPipeline` itself (it already has the scene CRC and name).
14. `DataPacker/Source/ExportJobs/ExportJob.cpp:137-146` `RunExport` — the `!mbDirty` cached-chunk fast path reads the payload with no post-read stream check (`CheckDirty` validates only the 16-byte header), so a chunk file truncated after its header silently packs a zero tail. Add `VERIFY_SUCCESS(fileStream.good())` (or a gcount check) and fall back to dirty re-export on failure. Residual gap of the executed `ExportJobCleanPathCachedChunkReadCheck` plan.
15. (Optional) `Engine/Source/Graphics/GraphicsUtils.cpp:50` `VkNameImpl` — for an unrecognized `VkObjectType`, `string_VkObjectType` returns "Unhandled VkObjectType" and the fixed +15 prefix strip yields the garbage tail "ObjectType" (in-bounds, `kbVulkanDebugLayers`-only). Guard on the expected prefix or leave with a comment.

## Critical files

Per item above — Ui (1, 2), Graphics render/wrappers (3), DataPacker (4, 8, 9, 14), vcxprojs (5), Input (6), game Collections (7), Graphics Objects/Managers (10–13), GraphicsUtils (15).

## Out of scope

- Anything needing a real design decision. No refactors, no adjacent cleanup — each item touches only its cited lines.
- Item 2 does not reintroduce the old hand-rolled editor; it only tunes the ImPlot interaction.

## Notes

- No determinism/CRC/`kiVersion`/wire exposure anywhere. Item 14 is DataPacker-only and byte-identical for valid inputs; items 3 and 10–13 are client/graphics-only; item 7 is a debug tripwire.
- Items 10–13 touch the pipeline-cluster files (`Pipeline.{h,cpp}`, `PipelineManager.cpp`, `DynamicPipelines.cpp`) — respect the pipeline-cluster sequencing in `Order.md` Dependencies (never interleave with `BindlessSlotLifecycle`; refresh citations if `Refactor_PipelineManagerSplit` lands first).
- No grill decisions (item 7's promote-vs-comment is resolvable inline; default: comment why Spaceships-only).
