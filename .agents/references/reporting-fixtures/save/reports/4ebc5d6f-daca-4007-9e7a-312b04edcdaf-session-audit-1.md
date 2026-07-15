Schema: be-agent-report/v1
Requested role: Fable/Sol session auditor
Actual executor: GPT-5 Codex (Sol role)
Fallback: none
Paired-review diversity: preserved
Worktree: <WORKTREE>
Session-start baseline: ca6f005addca80e8273cc7732e436fe351c1f71c
Plan/intent: <WORKTREE>\Documents\Plans\Save\ServerSaveFailureReporting.md; approved delta none; step-10 findings-only code-group audit

# Session Audit

Result: NEEDS_ACTION

## Scope and evidence read

Read each assigned file whole:

- `Engine/Source/File/DifferenceStream.h`
- `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsServer.cpp`
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp`
- `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp`
- `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.h`

Compared the final tree with baseline `ca6f005addca80e8273cc7732e436fe351c1f71c`, the complete approved plan, implementation X001-X005, the no-edit propagation sweep, late review fixes X001-X002, late style fix X001, and final verification V001-V011. Also inspected the direct `FileManager` and `GameBase::CurrentFrame` dependencies needed to test the finished replay-stop contract. `git diff --check` passed; the worktree has no untracked files.

## Findings

- F001 — `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp:349-353` — mode 5 — a recording writer can outlive its coord Frame, after which replay stop throws before that writer is saved and never reaches later writers, `mReplayWriters.clear()`, metadata, or the aggregate failure log — **structural**
  - Recording start snapshots every then-present `mCoordFrames` entry into `mReplayWriters` (`GameSaveLoad.cpp:310-314`), but recording does not freeze the active set: `ServerSession::PrepareTick` continues rebuilding it while recording and `SyncActiveFrames` erases Frames outside the new set (`ServerSession.cpp:44-67,401-417`). The recording-update loop explicitly tolerates this by skipping writers whose coord no longer exists (`GameSaveLoad.cpp:381-388`), leaving the writer resident.
  - Stop then evaluates `mrGameBase.CurrentFrame(rCoord)` as the argument to `Save`; `GameBase::CurrentFrame` uses `mCoordFrames.at(coord)`, so an evicted recorded coord throws `std::out_of_range` before `Save` runs. The stop flag was already cleared, while the uncleared writer map still makes `IsRecording()` true, so the recording can remain stuck as well as bypassing the plan's exhaustive-stop behavior.
  - A complete correction needs a defined end-state/eviction policy for recorded coords (for example, retaining the last complete end Frame or retaining recorded Frames through stop); merely catching and skipping the writer cannot produce the complete sibling set promised by the replay format. Route through step 11.

- F002 — `Engine/Source/File/DifferenceStream.h:73` — mode 7 — the final-tree claim that writer persistence failures return `false` and allow every later component to run is still false for backup-status OS errors — **small**
  - Replay stop passes `kBackup` into `DifferenceStreamWriter::Save` (`GameSaveLoad.cpp:352`), and `Save` forwards those flags to every sibling `WriteFileAtomically` call (`DifferenceStream.h:73,95,115,143`). Before returning its boolean, `WriteFileAtomically` calls `BackupExistingFile` (`FileManager.h:203-208`); that helper queries the existing target with the throwing `std::filesystem::exists(path)` overload (`FileManager.cpp:118-124`). Under an OS status error such as access denial, `filesystem_error` escapes before `Save` can return `false`.
  - The late `RemovePartialFile` helper only contains removal exceptions after an ordinary false write result; it cannot handle this pre-write backup exception. Consequently later siblings, later coordinate writers, writer clearing, metadata, and the aggregate diagnostic are skipped, the same externally visible failure shape the accepted cleanup fix was meant to prevent.
  - Use the error-code existence query (logging and treating backup as unavailable while preserving the documented main-write behavior), or equivalently contain the backup-status exception at that trust boundary, so `WriteFileAtomically` reaches its existing boolean result path. The C++ filesystem throw/report premise is already covered by external-verification report `<GUID>` A002's applicable `[fs.err.report]` rule.

## Failure-mode disposition

1. Fix-introduced desync: clean. Late NUL validation, cleanup exception containment, and the style-only lambda rename do not touch CRC state, update phases, float operation order, serialization bytes, or RNG draws.
2. Half-applied mirrored edits: clean. All `WriteGrid`, both `ServerSave`, and the sole `DifferenceStreamWriter::Save` consumer agree on the new boolean contract; agent save, client save, replay start, and replay stop consume results at their intended boundaries. No client/server, writer/reader-format, or optional-fullframes mirror was missed.
3. Doc/code drift from late renames: clean. Repository search found no `fnRemovePartialFile`, stale void signature, or stale symbol reference. This session created no `AGENTS.md`/`CLAUDE.md` pair.
4. Unreviewed late edits: clean apart from the separate pre-existing backup path in F002. The embedded-NUL check precedes every pointer-source path construction for agent save/load, and each late cleanup call independently catches `filesystem_error`; the style rename changed no behavior or guard affinity.
5. Whole-file incoherence: F001. The update path accepts evicted recorded coords while the stop path assumes they remain present.
6. Residual leakage: clean. Mixed-generation replay persistence and the PowerShell 5.1 provisioning hook are both retained below with their existing step-11 routing.
7. False completion: F002. The accepted NUL and removal-cleanup fixes are present, and V003-V008 exercise the documented ordinary/false-return branches, but V007's exhaustive continuation claim does not cover the still-throwing backup-status path.
8. Debris: clean. No diagnostic-only added logs, commented-out code, scratch source files, unexpected untracked files, or file-wide guard changes were found. Added warning/error logs are required failure diagnostics.

## Residuals retained

- R001 — mixed-generation replay persistence: a failed manifest write can leave a stale manifest with newer components because there is no set-level commit/invalidation marker. Pre-existing structural item, already routed to step 11.
- R002 — PowerShell provisioning hook: the vcxproj invokes Windows PowerShell 5.1 while `Provision-WorktreeThirdParty.ps1` uses `SHA256.HashData`; builds required direct PowerShell 7 provisioning plus disabling the redundant hook. Infrastructure item, already routed to step 11.

Files changed: none
Functions/regions touched: none
Residuals:
- R001 mixed-generation replay persistence, routed to step 11
- R002 PowerShell 5.1 provisioning-hook incompatibility, routed to step 11
