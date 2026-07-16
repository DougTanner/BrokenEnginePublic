# Structured WorktreeCli Build Results

## Context

`WorktreeCli build` currently preserves its public command syntax and target-level serialization, but its process contract is split across inherited console streams and a final exit code. `RunBuildCommand` prints WorktreeCli progress directly, launches MSBuild through `RunProcess(..., false)`, and lets MSBuild inherit the caller's stdout/stderr. Agents and scripts must therefore scrape interleaved human text to determine the target, outcome, diagnostics, data path, and retained log location. That is fragile across verbose builds and makes the compile workflow harder to consume deterministically.

AgentTools maintenance has a second coordination gap. The explicit primary-maintenance path builds canonical WorktreeCli and AgentHarness outputs in place while holding maintenance. A failed or incompatible tool build can therefore disturb the prebuilt executables needed by other registered sessions. Stage A made routine `/next-plan` execution session-worktree-only; this dependent stage uses that isolation to produce and verify AgentTools candidates before any guarded promotion to canonical primary output.

This is a Tier 3 build/bootstrap-coordination change. It affects a cross-session tool contract and the executables that wrappers depend on, but it does not affect game runtime behavior or data packing.

## Design

1. Preserve the existing `WorktreeCli build [--files ... --] <project-or-solution> <MSBuild arguments...>` syntax, exit-code meaning, per-target serialization, selective-object invalidation, job-object ownership, and 660-second lock wait. Replace the build command's mixed console output with exactly one schema-versioned JSON result on stdout: `broken-engine-build-result/v1`.

2. Make the result describe the complete invocation outcome without requiring console scraping. Include normalized target and worktree identities, requested build arguments and selected files, lock disposition, MSBuild discovery/launch state, process exit code, overall `success` or `fail` status, elapsed duration, retained log path, and structured diagnostics. Keep internal/tool failures distinct from a launched MSBuild failure while preserving the command's existing nonzero exit behavior.

3. Capture MSBuild stdout and stderr together in their observed read order and write the complete raw stream to an ignored file below the invoking worktree's `Temp/AgentBuildLogs/`. The file name must be collision-resistant and stable for the returned result. Do not truncate the retained log, echo raw MSBuild output to stdout, or write build logs into tracked source directories. A log-write failure is a visible build-result failure rather than an omitted side effect.

4. Add one canonical diagnostic reader at the WorktreeCli/compile boundary. Parse MSBuild error and warning records from the retained combined stream into structured entries containing severity, code when present, project/file/location when present, and the original diagnostic line. Preserve unmatched fatal tool lines as structured build messages. The reader must not change MSBuild success/failure; it only provides deterministic evidence and human-readable summaries.

5. Update the `/compile` skill and WorktreeCli documentation to consume the JSON contract and retained log. Delegated builds report overall/per-target status, exit code, relevant structured errors and changed-file warnings, data-mode evidence, and the exact raw-log path. They no longer treat inherited terminal output as the authoritative result. Keep synchronous foreground execution and existing retry/timeout guidance.

6. Rework explicit AgentTools primary maintenance into candidate then promotion. Build WorktreeCli and AgentHarness into session-owned candidate output, validate both candidates through their capability contracts, and record candidate hashes and source commit before acquiring promotion authority. Never overwrite or delete canonical primary executables during candidate production.

7. Promote a validated candidate pair only during an approved `session-landing`, after reconciliation and final verification bind the same source tree. Under the existing PC-global landing and maintenance coordination, snapshot the canonical pair, atomically replace both executables as one logical promotion, rerun capability checks from canonical paths, and return a schema-versioned receipt containing previous/candidate/promoted identities. If either replacement or post-promotion check fails, restore the complete previous pair and verify the rollback before releasing coordination. A partial pair is always a blocker.

8. Extend finalization evidence and fixtures for the candidate/promotion boundary. Cover successful pair promotion, candidate/source mismatch, another registered session or stale authority blocking promotion, first/second replacement failure, failed post-promotion capability validation, complete rollback, and receipt identity. Promotion must remain impossible from ordinary routine builds or an unlanded session tree.

## Critical files

- `Tools/WorktreeCli/BuildCommand.cpp` and `BuildCommand.h` — build-process capture, retained log, diagnostics, and `broken-engine-build-result/v1` emission while preserving the public CLI.
- `Tools/WorktreeCli/WorktreeCli.cpp` and `Tools/WorktreeCli/AGENTS.md` — usage and public result-contract documentation.
- `.agents/skills/compile/SKILL.md` and `.agents/skills/compile/scripts/` — JSON consumption, deterministic reporting, and candidate AgentTools build/validation.
- `.agents/skills/finalize-changes/SKILL.md` and `.agents/skills/finalize-changes/scripts/` — approved session-landing promotion, rollback, receipts, and fixtures.
- `.agents/scripts/Test-AgentToolsCapabilities.ps1` and wrapper/bootstrap callers — pair capability validation and canonical-output identity checks.
- `.gitignore` — ignore `Temp/AgentBuildLogs/` if the existing `Temp/` rules do not already cover it.

## Out of scope

- Changing `WorktreeCli build` command-line syntax, MSBuild properties, target selection, selective compile semantics, or exit-code compatibility.
- Changing DataPacker execution, export, mutex, cache, `.pack`/manifest production, or generated-data behavior.
- Streaming structured progress events, background builds, remote build execution, or a general-purpose process protocol.
- Parsing every possible third-party compiler message beyond the diagnostics needed by current Visual Studio 2026/MSBuild builds.
- Promoting AgentTools outside an explicitly approved session landing, copying candidates between unrelated sessions, or weakening wrapper session and landing locks.
- C++ game/runtime, deterministic simulation/CRC, replay, wire protocol, serialization layout, `kiVersion`, client/server guard affinity, shaders, or allocation-tracked paths.

## Acceptance criteria

- Existing full and `--files` build invocations retain their syntax, serialization, target behavior, and exit-code meaning, while stdout contains exactly one valid `broken-engine-build-result/v1` object.
- Every launched MSBuild invocation writes its complete combined raw output to the returned ignored `Temp/AgentBuildLogs/` path; the JSON result exposes decisive status, exit code, duration, and structured diagnostics without log scraping.
- Success, compiler error, warning, missing target/MSBuild, launch failure, lock timeout, and retained-log failure fixtures produce deterministic schema-valid results and appropriate process exits.
- `/compile` consumes the structured result synchronously and reports the required errors, changed-file warnings, data-mode evidence, and raw-log locator.
- AgentTools candidates are built and capability-checked without modifying canonical primary outputs; candidate receipts bind both executable hashes to the reconciled source commit.
- An approved session landing promotes both validated executables atomically under existing coordination. Any replacement or post-promotion failure restores and revalidates the previous complete pair, with no partial canonical state.
- Existing finalization/landing behavior remains unchanged for sessions whose manifest does not require AgentTools promotion.
- WorktreeCli and affected PowerShell fixtures pass, changed skills pass `/validate-skill`, and the final acceptance ledger records the build-result and promotion/rollback contracts.

## Notes

- Queue metadata: Tier Large; Effort 4; Impact 3; Risks 3; Score 4.
- Landing boundary: `.agents/skills/finalize-changes/scripts/Invoke-FinalizeApprovalPreparation.ps1` now provides the single verified pre-approval preparation command that this plan extends with guarded AgentTools promotion.
- Risk triggers: Tier 3 cross-session build/bootstrap coordination, AgentTools canonical-output mutation, session landing, and final-evidence integration. The execution card must bind the structured-result schema, candidate pair, reconciled source commit, previous canonical pair, promotion receipt, and rollback checks.
- No material architectural choice remains for planning. If implementation discovers that combined stdout/stderr ordering cannot be retained with one Windows pipe or that two-file promotion cannot meet the atomic/rollback acceptance contract with existing locks, stop for a user decision rather than weakening the contract.
