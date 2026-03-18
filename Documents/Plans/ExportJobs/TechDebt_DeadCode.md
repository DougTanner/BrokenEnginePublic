# Tech Debt: Dead Code

Source: /external-tech-debt on DataPacker/Source/ExportJobs

## Changes

### DataPacker/Source/ExportJobs/ExportSceneVertices.h
- Remove `AncestorJointResult` struct (lines 35-39) — never referenced anywhere in codebase
- Remove `FindNearestAncestorJoint` declaration (line 42) — never called

### DataPacker/Source/ExportJobs/ExportSceneVertices.cpp
- Remove `FindNearestAncestorJoint` function definition (lines 52-78) — never called

### DataPacker/Source/ExportJobs/ExportSceneAnimation.cpp
- Remove unused `iAnimIndex` variable declaration (line 30) and increment (line 177)

### DataPacker/Source/ExportJobs/ExportScene.cpp
- Move `gGltfContext` (line 10) from file-scope global to local variable inside `ExportScene::LoadGltfModel()` — it is only used there

## Verification Notes
- All file paths exist and all line numbers match the actual source code
- `AncestorJointResult` and `FindNearestAncestorJoint` are confirmed unreferenced via codebase-wide grep — safe to remove
- `iAnimIndex` is declared (line 30) and incremented (line 177) but never read — pure dead code
- `gGltfContext` is only used at line 129 inside `LoadGltfModel()` — making it local reduces unnecessary file scope
