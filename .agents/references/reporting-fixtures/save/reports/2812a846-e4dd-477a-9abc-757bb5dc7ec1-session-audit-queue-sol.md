Schema: be-agent-report/v1
Requested role: Sol/Fable session-audit reviewer
Actual executor: GPT-5 Codex (Sol role)
Fallback: none
Paired-review diversity: preserved
Worktree: <WORKTREE>
Session-start baseline: ca6f005addca80e8273cc7732e436fe351c1f71c
Plan/intent: Documents/Plans/Save/ServerSaveFailureReporting.md (approved plan executed and now deleted); approved delta none; final queue cleanup group

# Session Audit — Queue Cleanup Group

Result: NEEDS_ACTION

## Findings

### F001 — Replay commit plan cannot meet its unconditional stop acceptance before the recorded-coordinate policy lands

`Documents/Plans/Save/ReplayGenerationCommitAtomicity.md:38` — mode 6 — the plan unconditionally requires stop to attempt every coordinate writer and metadata write, clear writers, and emit the aggregate result, while `Documents/Plans/Save/RecordedCoordReplayStopPolicy.md:17` explicitly owns the end-state policy needed when a recorded coordinate has been evicted and rejects catch-and-skip. Current code proves the dependency: `ServerSession::SyncActiveFrames` erases inactive `mCoordFrames` entries (`Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp:412-416`), then stop unconditionally calls throwing `CurrentFrame(rCoord)` (`Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp:349-353`; accessor at `Engine/Source/GameBase.h:178-181`). Yet `Documents/Plans/Order.md:161` records only warning-only overlap and `## Dependencies` contains no directional or joint-landing constraint. `/next-plan` can therefore execute `ReplayGenerationCommitAtomicity` first, where it must either expand into the other plan's structural lifetime policy or fail its own acceptance criterion — **small**. Add a directional prerequisite that `RecordedCoordReplayStopPolicy` lands first, or make the two a mandatory joint landing; align the atomicity acceptance wording with that constraint.

## Pre-existing residual

### R001 — File Group names a plan that no longer exists

`Documents/Plans/Order.md:160` — mode 6 — the Release static-analysis File Group says `Engine/Architecture_LibraryReplacement.md` overlaps `ImGuiManager.cpp`, but that file has no live row and does not exist anywhere under `Documents/Plans` or `Documents/Features`. Git history confirms it is a previously deleted plan, and the stale reference was already present at the session baseline. This violates the live-only `Order.md` contract and the requested no-stale-metadata check — **small**. Remove the obsolete `Architecture_LibraryReplacement.md` overlap clause while retaining the live PackChunks and Profile overlaps.

## Clean checks

- Mode 1: no assigned code/CRC-state edits; not applicable. The new replay plans explicitly preserve sim/CRC/version boundaries.
- Mode 2: row/file and coordination mirrors were checked. All 85 executable rows map to existing files exactly once; both reference/index rows map to existing files; no orphan executable/reference plan file exists. The selected plan is absent from the live row, Dependencies, File Groups, and the surviving plan's coordination warnings. F001 is the one cross-plan constraint gap.
- Mode 3: no changed AGENTS.md/CLAUDE.md pair or late symbol rename belongs to this group. New-plan code citations were spot-checked against current `SyncReplayTick`, `SaveLoadReplay`, `SyncActiveFrames`, `CurrentFrame`, the six vcxproj pre-build events, and `WorktreeCliSessionExclusion.psm1`; they remain actionable.
- Mode 4: the late cleanup edits to `Order.md`, `Architecture_GameBaseDeadVirtuals.md`, and the selected-plan deletion were read whole. The three step-11 additions remain intact; no late edit added guard/project-affinity work.
- Mode 5: required headings exist in all three new plans; row labels/links match; score arithmetic is correct; scores are nondecreasing; the GameSaveLoad and server-manager File Groups name all intended live plans. The two remaining references to the deleted selected plan occur only in the new plans' substantive Context, restate the inherited current contract, and are not queue coordination metadata.
- Mode 6: step-11 residuals C001-C003 all have live, scored, actionable plan files and rows. F001 and R001 are reported above.
- Mode 7: cleanup X001-X003 exist in the current tree. Direct `plan row status` confirms the deleted selected row's claim remains owned by `<GUID>`, session `next-plan`, for this exact worktree; it was not prematurely unclaimed. R001 is a broader pre-existing reference defect not covered by those selected-plan-specific claims.
- Mode 8: `git diff --check -- Documents/Plans` passes; no scratch files, changelog annotations, duplicate rows, stale claim metadata, or selected-plan coordination debris were introduced.

## Stable decision index

- F001 | small | Documents/Plans/Save/ReplayGenerationCommitAtomicity.md:38; Documents/Plans/Order.md:161 | Add recorded-coordinate policy prerequisite or mandatory joint landing so replay atomicity can meet exhaustive stop acceptance without scope expansion.
- R001 | pre-existing small | Documents/Plans/Order.md:160 | Remove stale File Group reference to missing `Engine/Architecture_LibraryReplacement.md`.

Files changed: none
Functions/regions touched: none
Residuals:
- R001 — pre-existing stale `Engine/Architecture_LibraryReplacement.md` File Group reference at `Documents/Plans/Order.md:160`.
