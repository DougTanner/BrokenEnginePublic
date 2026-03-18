# Refactor: Simplifications & Function Decomposition

Source: /external-refactor-clean on DataPacker/Source/ExportJobs

## Changes

### DataPacker/Source/ExportJobs/ExportSceneVertices.cpp
- Fix typo: rename `bOcculsion` to `bOcclusion` in `IsOcclusion()` function (lines 416, 419, 421, 424)

### DataPacker/Source/ExportJobs/ExportSceneSkeleton.cpp
- Consolidate `BuildNodeSkeleton()` (lines 16-142) and `LoadSkeleton()` (lines 144-262) into a single function. They share ~80% of their code: both process ALL nodes with identical TRS/matrix loading, both load inverse bind matrices, both build skinJointToNode. The only difference is `BuildNodeSkeleton` also builds an identity nodeToJointMap. Suggested signature: `SkeletonData LoadSkeletonData(const tinygltf::Model& rModel, int32_t iSkinIndex, std::unordered_map<int, int>* pNodeToJointMap)` where pNodeToJointMap is filled when non-null
- Extract shared node-processing helper for the TRS + matrix loading loop that is identical between the two functions (BuildNodeSkeleton lines 82-139, LoadSkeleton lines 200-259)

### DataPacker/Source/ExportJobs/ExportScene.cpp
- Extract occlusion flag computation into a helper: `std::vector<bool> ComputeOcclusionFlags(const tinygltf::Model& rModel)` — eliminates duplication between PreExport (lines 168-182) and MainExport (lines 456-464)
- Cache `DetermineAnimationPath()` result — currently called twice (line 223 in PreExport, line 597 in MainExport) on the same model. Store result in a member variable or pass between phases
- Extract identity nodeToJointMap building into a helper — same loop appears at lines 228-231 and 607-611

### DataPacker/Source/ExportJobs/ExportTexture.cpp
- Extract KTX cubemap load + half-to-float conversion into a shared helper function. The block at lines 48-74 (GenerateIrradianceCubemaps) and lines 201-228 (GeneratePreFilteredCubemaps) are identical: load gli texture, validate cube target + RGBA16F format, convert half→float per face. Suggested: `std::pair<std::vector<float>, uint32_t> LoadKtxCubemapAsFloat(const std::filesystem::path& path)`

## Verification Notes
- All file paths exist and line numbers verified against source
- Typo `bOcculsion` confirmed at ExportSceneVertices.cpp lines 416, 419, 421, 424
- `BuildNodeSkeleton` (lines 16-142) and `LoadSkeleton` (lines 144-262) confirmed to share ~80% identical code; consolidation is the highest-value item in this plan
- `DetermineAnimationPath()` confirmed called twice on the same model (PreExport line 223, MainExport line 597)
- Minor correction: identity nodeToJointMap in MainExport starts at line 608 (not 607)
- Items 2-6 overlap with TechDebt_Duplication.md which identifies the same duplication; this plan provides the implementation strategy
- KTX cubemap blocks confirmed identical except for surrounding context (irradiance vs radiance filter call)
