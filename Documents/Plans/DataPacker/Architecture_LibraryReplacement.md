# Architecture: Library Replacement — meshoptimizer Vertex Remap

## Context
Source: /external-architecture-review on `DataPacker/Source/ExportJobs` (recursive). The area is already aggressively library-backed — the only actionable candidate is extending coverage of an **already-imported** library. All other clusters were evaluated and rejected (too small, engine-specific by design, or would require a new heavy import); CGAL and Qhull were rejected on license grounds for the convex-hull code, which stays in-house.

## Design

### Replace hand-rolled vertex dedup with meshoptimizer remap
- **Module / cluster**: manual vertex de-duplication via `std::unordered_map<common::ModelVertex, uint32_t>` + index remap in `LoadVertices` (`Scene/SceneVerticesLoader.cpp:233-296`), plus the remap application (:301-345) and the `std::hash<common::ModelVertex>` specialization in Common it depends on.
- **Lines removable**: ~30-45.
- **Proposed library**: **meshoptimizer** — MIT license (allow-listed), already in `/ThirdParty/meshoptimizer/` and already linked by sibling `ExportScene.cpp` (`meshopt_optimizeVertexCache`/`Overdraw`/`VertexFetch`). Use `meshopt_generateVertexRemap` + `meshopt_remapIndexBuffer` + `meshopt_remapVertexBuffer` — the canonical battle-tested implementation of exactly this dedup-and-remap. Zero new imports, zero license work.
- **Risks**: requires restructuring — current code dedups incrementally per primitive while appending into a shared scene-wide buffer; meshopt remap operates on a fully built buffer (build raw vertices per primitive, remap, then append). `meshopt_generateVertexRemap` compares raw bytes, so `common::ModelVertex` padding must be deterministically zeroed (the existing hash-based map already has this requirement — low marginal risk). Output vertex ordering will differ from today's insertion order → `.MODEL` bytes and downstream CRC-referenced chunks change → bump `ExportScene::GetVersion`/`ExportModel::GetVersion`, one full scene re-export. Offline tool; no runtime impact.
- **Confidence**: MEDIUM. [~1h]

## Critical files
- `DataPacker/Source/ExportJobs/Scene/SceneVerticesLoader.cpp` (`LoadVertices` dedup/remap blocks)
- `DataPacker/Source/ExportJobs/ExportScene.h` / `ExportModel.h` (version bumps)
- The `std::hash<common::ModelVertex>` specialization at `Common/DataFile.h:393-400` (delete if this was its only consumer — verify; also retire the companion comment block at `DataFile.h:386-388` that documents the hash/equality pairing)

## Out of scope
- New third-party imports — none proposed; this extends an existing import.
- Convex hull (`BuildValidAreaHull`), beach-band subdivision, BMFont parsing, depfile parsing, half↔float loops, BCn decode — all evaluated and rejected (too small / engine-specific / better handled as API-usage cleanups in other plans).
- Replacing the glslc/glslangValidator/spirv-opt subprocesses with shaderc/glslang libraries — heavy build-system change for negative value.

## Notes
- Changes `.MODEL`/chunk bytes (vertex order) — requires the version bumps named above; one-time full scene re-export. No runtime determinism/CRC-sim exposure.
- File-group overlap: `SceneVerticesLoader.cpp` is also touched by `Architecture_SceneLoaderVestigialPaths.md` and `Refactor_SceneFunctionDecomposition.md` — execute this either together with or after the `LoadVertices` decomposition (the decomposition makes the remap restructure easier).

## Verification Notes
- License and import claims re-verified: meshoptimizer is MIT, on the `ThirdParty/CLAUDE.md` allow list, already imported at `/ThirdParty/meshoptimizer/`, and already used by `ExportScene.cpp` (`meshopt_optimizeVertexCache`/`Overdraw` at :339-340, `meshopt_optimizeVertexFetch` at :474). All three proposed functions (`meshopt_generateVertexRemap`, `meshopt_remapIndexBuffer`, `meshopt_remapVertexBuffer`) exist in the imported version (`ThirdParty/meshoptimizer/src/meshoptimizer.h`, implementations in `src/indexgenerator.cpp`).
- Dedup-block citations exact (`SceneVerticesLoader.cpp:233-296` map-based dedup, :301-345 remap application). Padding risk is smaller than the plan implies: `common::ModelVertex` is locked padding-free by `static_assert(sizeof == 100)` at `DataFile.h:389`, so meshopt's raw-byte comparison has no uninitialized-padding hazard at all.
- One genuine semantic delta to carry into the grill: meshopt compares raw bytes, while the current `operator==` (`DataFile.h:365-373`) treats `+0.0 == -0.0` (and would treat equal-value/different-payload NaNs as unequal). The current code *already* violates the hash/equality contract for the signed-zero case (byte-hash vs field-equality), so signed-zero vertices effectively fail to dedup today too — practical behavior is unchanged, but the plan's "battle-tested implementation of exactly this dedup" should be read as byte-identity dedup, not float-equality dedup.
