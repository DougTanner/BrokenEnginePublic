# Refactor: ExportScene::CheckDirty Filesystem Side Effect

## Context
Source: /external-refactor-clean on `DataPacker/Source/ExportJobs` (recursive), confirming a Phase-1 handoff. `ExportScene::CheckDirty` deletes the `.PreExport` marker inside a dirty *check* (`ExportScene.cpp:57-63`) — a side-effecting predicate. The deletion is currently load-bearing: `Export()`'s `bNeedsPreExport` probe (:215-223) only inspects the marker's *version*, not input freshness, so without the delete a dirty main input with a current-version marker would incorrectly skip `PreExport`.

## Design

### Replace the deletion with explicit state
- In `ExportScene::CheckDirty`, set a member flag (e.g. `mbNeedsPreExport`) instead of deleting the marker file (`ExportScene.cpp:57-63`); have `Export()`'s probe (:215-223) consult the flag in addition to the marker-version check. The predicate becomes side-effect-free and the control flow self-documenting [~20m]

### Dedup the marker-version read
- The `.PreExport` marker-version read is duplicated verbatim between `CheckDirty` (:67-83) and `Export` (:216-223) — extract a private `ReadPreExportMarkerVersion()` used by both [~10m]

## Critical files
- `DataPacker/Source/ExportJobs/ExportScene.h` (new member flag + helper declaration)
- `DataPacker/Source/ExportJobs/ExportScene.cpp` (`CheckDirty`, `Export`)

## Acceptance criteria
- A dirty scene input with a current-version `.PreExport` marker still re-runs `PreExport` (the behavior the deletion provided).
- A stale-version marker still forces `PreExport` even when the main chunk is clean (existing two-phase contract, `ExportJobs/CLAUDE.md` §Scene Two-Phase).
- `CheckDirty` performs no filesystem writes/deletes.

## Out of scope
- The two-phase PreExport/`.MODEL` design itself — documented intentional.
- Other `CheckDirty` overrides (`ExportIsland`, `ExportShader`) — no side effects there.

## Notes
- No pack-byte/version exposure — control-flow refactor only; the same exports run in the same cases.
- File-group overlap: `ExportScene.cpp` is touched by several plans in this batch — co-schedule.

## Verification Notes
- The load-bearing claim re-derived and confirmed: the deletion (`ExportScene.cpp:57-62`) happens *only* in the `bDirty` branch — `CheckDirty` never deletes the marker when it returns false overall, so the proposed flag has no clean-path divergence to worry about. `Export()`'s probe (:215-223) checks marker version only; without the deletion (or the flag), a dirty main input with a current-version marker would skip `PreExport` and reuse a stale `.MODEL`.
- Flag-replacement behavior checked across the failure paths too: each `ExportScene` instance is constructed fresh per run (flag defaults false); on export throw after `PreExport`, `CleanupOnFailure` (:831-838) already unlinks the rewritten marker; on throw before `PreExport`, the old pack is preserved on failure (`Main.cpp:456-457` removes only temp files), so the next run's base `CheckDirty` re-derives dirty and re-sets the flag — equivalent end state to today's eager deletion in every path. The flag only needs setting in the `bDirty` branch; the stale-marker branch (:67-83) is already covered by `Export()`'s own version probe.
- The marker-version read duplication (:67-83 vs :215-223) confirmed effectively verbatim (same open/read/validate shape) — the `ReadPreExportMarkerVersion()` extraction is sound; have it return `std::optional<int64_t>` (or a missing/bad sentinel) so both call sites keep their distinct missing-file handling.
