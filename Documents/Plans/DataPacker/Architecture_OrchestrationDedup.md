# Architecture: Export Orchestration Dedup (IBL Pre-Pass Loops, CleanupOnFailure)

## Context
Source: /external-architecture-review on `DataPacker/Source/ExportJobs` (recursive). The IBL cubemap pre-pass functions bypass the `ExportJob` pipeline (by design — their outputs are intermediates, not chunks) but re-implement its concerns three times over inside one file: discovery scan, `[C]`/extension matching, timestamp dirty-check. Separately, two jobs duplicate the base-worthy intermediate-cleanup pattern verbatim.

## Design

### Unify the three IBL discovery/dirty loops
- `ExportCubemapIbl.cpp` contains three near-identical recursive-scan + match + timestamp-dirty-check loops: irradiance `.ktx` (:57-79), prefiltered `.ktx` (:189-209), prefiltered face-directory (:233-280). Extract one scan helper parameterized by match predicate and output-path naming, leaving each pass as a predicate + per-hit callback [~45m]

### Merge the duplicated intermediate write
- The legacy-shaped `[w][h][mips]` intermediate write is duplicated between the irradiance path (:109-120) and the `convertAndWrite` lambda (:141-186). Merge into one write function used by both [~20m]

### Hoist intermediate-cleanup into the `ExportJob` base
- `CleanupOnFailure` + `mIntermediateFiles` tracking is duplicated verbatim in `ExportShader` (`ExportShader.cpp:429-436`) and `ExportScene` (`ExportScene.cpp:831-838`). Move the member and the default unlink-on-failure implementation into `ExportJob` (`ExportJob.h`), keeping `CleanupOnFailure` virtual for jobs needing extra behavior [~20m]

## Critical files
- `DataPacker/Source/ExportJobs/ExportCubemapIbl.cpp`
- `DataPacker/Source/ExportJobs/ExportJob.h`, `ExportJob.cpp`
- `DataPacker/Source/ExportJobs/ExportShader.{h,cpp}`, `ExportScene.{h,cpp}` (delete the duplicated members/overrides)

## Out of scope
- Converting the IBL pre-passes into `ExportJob` subclasses — they produce intermediates, not chunks; the free-function shape is correct, only the internal duplication goes.
- `MigrateLegacyIntermediates`' own discovery/dirty loop — one-shot legacy-migration path, not worth coupling to the IBL helper.
- The IBL half↔float conversion loops — covered by `Refactor_ExportJobsQuickWins.md`.
- Output format or face-major/mip-minor ordering — bytes unchanged.

## Notes
- No determinism/CRC/pack-layout exposure: refactor of scan/orchestration code only; emitted intermediate bytes identical.
- File-group overlap: `ExportCubemapIbl.cpp` is also touched by `Refactor_ExportJobsQuickWins.md` (stream conversions) and `Architecture_PackByteDeterminism.md` — co-schedule.

## Verification Notes
- Verified — all citations exact. Three IBL loops at :57-79 / :189-209 / :233-280; duplicated `[w][h][mips]` write at :109-120 (irradiance) vs :174-185 (inside `convertAndWrite`, :141-186); `CleanupOnFailure` byte-identical between `ExportShader.cpp:429-436` and `ExportScene.cpp:831-838`, with the `mIntermediateFiles` member duplicated at `ExportShader.h:50` / `ExportScene.h:53` and the empty virtual hook already present at `ExportJob.h:45`.
- One asymmetry to mind when extracting the scan helper: the two `.ktx` passes dirty-check output-vs-single-source mtime (:76, :206), while the face-directory pass checks output against all 6 face files (:264-280) and also gates on face-file existence (:248-253) — the helper's predicate/dirty-check parameterization must cover both shapes.
- Caveat on the intermediate-write merge: the prefiltered path's `convertAndWrite` also does the mip-offset walk + half conversion (:144-172) before the write; only the trailing file-write block (:174-185) is the true duplicate of :109-120. The half-conversion loops themselves are owned by `Refactor_ExportJobsQuickWins.md` — keep the seams compatible if both land in one session.
