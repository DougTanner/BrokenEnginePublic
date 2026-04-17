# Refactor: ExportScene Decomposition

Source: /external-refactor-clean on DataPacker/Source/ExportJobs/

Goal: `ExportScene.cpp` is 736 lines — approaching the `/reduce-file` threshold. Two monster functions drive the size. Decompose them along their natural internal sections.

## Changes

### DataPacker/Source/ExportJobs/ExportScene.cpp
- Line 181 `ExportScene::PreExport` (spans 181-446, ~265 lines): decompose into private helpers on `ExportScene`:
  - `ProcessTextures(tinygltf::Model&)` — async texture processing block [~20m]
  - `BuildMaterials(tinygltf::Model&)` — material construction + skeleton-decision block [~20m]
  - `LoadVerticesAndOptimizeMeshes(tinygltf::Model&)` — vertex loading + meshopt calls [~20m]
  - `WriteModelFile(...)` — bounding box + 16-bit index conversion + file write [~15m]
- Line 448 `ExportScene::MainExport` (~280 lines): decompose into:
  - `ReadMaterialInfosFromModel(...)` [~15m]
  - `FillMaterialShaderDatas(...)` [~15m]
  - `WriteAnimationSection(...)` — animation-section block (confirm exact range by reading MainExport) [~20m]
- Line 315 `else if (!rInfo.bHasSkinning && rInfo.iNodeIndex >= 0)`: `!rInfo.bHasSkinning` is redundant — the preceding branch at line 299 is `if (rInfo.bHasSkinning)` with no intervening code that modifies `rInfo`, so reaching the `else if` already implies `!rInfo.bHasSkinning`. Simplify to `else if (rInfo.iNodeIndex >= 0)` [~5m]

## Expected Outcome

- `ExportScene.cpp` drops well under 500 lines after extractions
- Each helper is individually reviewable; future bug reports land on one 30-40 line function rather than a 280-line blob
- Defensive `!rInfo.bHasSkinning` check stops suggesting that path is reachable when it is not

## Verification Notes

- Verified `ExportScene.cpp` is 736 lines. `PreExport` at 181, `MainExport` at 448.
- Verified the `!rInfo.bHasSkinning` check at line 315 is redundant. Confirmed by reading lines 290-320: the preceding `if (rInfo.bHasSkinning)` at line 299 is the exact complement — no early `continue`, no reassignment of `rInfo.bHasSkinning` before the `else if`.
- Softened the `WriteAnimationSection` line range claim ("already a natural self-contained unit" at lines 585-726) since exact boundaries should be determined during implementation — the outer claim (animation section is decomposable) stands.
