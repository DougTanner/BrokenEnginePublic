# Tech Debt: Code Duplication

Source: /external-tech-debt on DataPacker/Source/ExportJobs

## Changes

### DataPacker/Source/ExportJobs/ExportSceneSkeleton.cpp
- Replace local parent map building in `LoadSkeleton` (lines 163-170, uses `unordered_map<int64_t,int64_t>`) with a call to `BuildNodeParentMap()` (already defined at lines 3-14, returns `unordered_map<int,int>`). The type difference is cosmetic — both use identity int-to-int mapping
- Extract shared node-processing logic from `BuildNodeSkeleton` (lines 82-139) and `LoadSkeleton` (lines 200-259) into a helper function. These blocks are nearly identical: iterate all nodes, set parent index, load TRS, load matrix. Only difference is the parent map type. See Refactor_Simplifications.md for consolidation approach
- Extract shared inverse-bind-matrix loading from `BuildNodeSkeleton` (lines 40-62) and `LoadSkeleton` (lines 172-198) into a helper function. See Refactor_Simplifications.md for consolidation approach

### DataPacker/Source/ExportJobs/ExportScene.cpp
- Extract identity `nodeToJointMap` building (repeated at lines 228-231 in PreExport and lines 608-611 in MainExport) into a shared helper or use the one already built by `BuildNodeSkeleton`. See Refactor_Simplifications.md for details
- Extract occlusion flag computation (PreExport lines 168-182, MainExport lines 456-464) into a helper that returns a `vector<bool>` given a model — both loops call `IsOcclusion()` the same way. See Refactor_Simplifications.md for suggested signature

### DataPacker/Source/ExportJobs/ExportTexture.cpp
- Extract KTX cubemap load + half-to-float conversion (used in both `GenerateIrradianceCubemaps` lines 48-74 and `GeneratePreFilteredCubemaps` lines 201-228) into a shared helper function that returns the float data vector. See Refactor_Simplifications.md for suggested signature

## Verification Notes
- All file paths exist and all line numbers verified against source
- Minor correction: identity nodeToJointMap in MainExport starts at line 608 (not 607)
- Items in ExportSceneSkeleton.cpp, ExportScene.cpp, and ExportTexture.cpp overlap with Refactor_Simplifications.md which provides concrete implementation details (function signatures, consolidation strategies). This plan identifies the duplication; Refactor_Simplifications.md prescribes the solution
- The `BuildNodeParentMap()` reuse is a standalone quick win independent of the larger skeleton consolidation
