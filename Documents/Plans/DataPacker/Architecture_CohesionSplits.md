# Architecture: Cohesion Splits

Source: /external-architecture-review on DataPacker/Source/

Goal: two files currently bundle unrelated responsibilities. Split each so its remaining surface becomes a cohesive "deep module" (Ousterhout).

## Changes

### DataPacker/Source/FileManager.{h,cpp} — extract license copier and Vulkan SDK path
`FileManager` currently owns input/output/temp paths + Vulkan SDK lookup + 92-line ThirdParty license copier. Split into three cohesive units.

- Extract `CopyThirdPartyLicenses` (`FileManager.cpp:73-164`) into a new `DataPacker/Source/Attribution.{h,cpp}` with a single free function `attribution::CopyThirdPartyLicenses(const std::filesystem::path& outputDir)` [~30m]
- Remove `mVulkanSdkBinariesDirectory` from `FileManager` (`FileManager.h:12`). Move the lookup (`FileManager.cpp:29-39`) into `ExportShader.cpp` as a file-scope static initialized from `VK_SDK_PATH` — it is the only consumer (three reads at `ExportShader.cpp:118,165,207`) [~20m]
- Update `ExportShader.cpp` references (three call sites) from `gpFileManager->mVulkanSdkBinariesDirectory` to the new local [~5m]

### DataPacker/Source/ExportJobs/ExportSceneAnimation, ExportSceneSkeleton, ExportSceneVertices — not ExportJobs, rename
These three `.h`/`.cpp` pairs contain zero `ExportJob` subclasses. They are free-function glTF loaders (`LoadAnimations`, `LoadSkeletonData`, `LoadVertices`) invoked only by `ExportScene.cpp`. The `Export*` naming misleads grep + the `RunExportJobs` reader into thinking they participate in the job pipeline.

- Move all three file pairs from `DataPacker/Source/ExportJobs/` into a new `DataPacker/Source/ExportJobs/Scene/` subdirectory [~10m]
- Rename each to drop the `Export` prefix: `SceneAnimationLoader.{h,cpp}`, `SceneSkeletonLoader.{h,cpp}`, `SceneVerticesLoader.{h,cpp}` [~10m]
- Update `ExportScene.cpp` + `ExportScene.h` includes accordingly [~5m]
- Update `DataPacker.vcxproj` / `.vcxproj.filters` to reflect new paths [~10m]

## Expected Outcome

- `FileManager` shrinks to input/output/temp directory resolution + CLI parsing — one concern
- Grep for `class.*: public ExportJob` matches exactly the jobs that `RunExportJobs` orchestrates

## Verification Notes

- REMOVED the "`Texture` encoder statics → free functions in `TextureEncode.{h,cpp}`" bullet. Investigation: all five static methods (`PixelToUint32`, `ToBc4`, `ToBc7`, `ToR8G8B8A8`, `ToR16`) are called ONLY from within `Texture.cpp` itself (lines 306-309, 329, 385-397). No external caller exists. The stated justification — "encoder helpers reusable without instantiating a float-pixel buffer" — is speculative (YAGNI violation). Moving them would be pure code churn with zero call-site benefit today. If a future consumer emerges, extract at that point.
- By extension, the `Texture::StaticInit()` file-scope initializer move is REMOVED. `StaticInit()` is called exactly once from `Main.cpp:188` before any job runs, which is a trivial ordering contract, not a design flaw. Keep as-is.
- Verified `CopyThirdPartyLicenses` at `FileManager.cpp:73-164` (92 lines) — extraction is worthwhile.
- Verified `mVulkanSdkBinariesDirectory` consumers — only `ExportShader.cpp:118,165,207` read it (also referenced in the unrelated plan file `SpirvOptIntegration.txt`). Local-to-ExportShader move is safe.
- Verified all three `ExportScene*` helper files contain only free functions — no `ExportJob` subclasses.
