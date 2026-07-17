# RunProcess Streaming Consolidation

## Context

Findings originate from a second-pass multi-agent review and are unverified claims; the executing agent must confirm each one before editing.

Commit 80c385bc (structured WorktreeCli build results) reintroduced ~85 lines in `Tools/WorktreeCli/BuildCommand.cpp` — `LaunchJobProcess` (`BuildCommand.cpp:100-148`, `CreateProcessW` at `BuildCommand.cpp:124`) plus `CreateOutputPipe` (`BuildCommand.cpp:150-170`) — that near-duplicate `toolcli::RunProcess`'s job-object/pipe/suspend/assign/resume machinery (`Tools/ToolCommon/ToolCliCommon.cpp:66-182`; `RunGit` at `:184-197`), hours after commit 76c282d1 consolidated exactly that into ToolCommon. The only capability `RunProcess` lacks is a streaming output sink — `RunMsBuildToLog` (`BuildCommand.cpp:377-415`) needs each read chunk fed incrementally to the retained log and `DiagnosticParser` — where `RunProcess` only appends into a buffered `ProcessResult::output` string (`ToolCliCommon.cpp:163-171`).

Two trivial adjacent findings in the same TUs: `DiagnosticParser::Consume` accumulates `mCarry` unboundedly until a newline arrives (`BuildCommand.cpp:264-274`) even though oversized lines are already skipped at the existing `kuiMaxDiagnosticLineLength` cap (`BuildCommand.cpp:31-34`); and `DataPacker/Source/DiagnosticReporter.cpp` carries vestigial `AddChecked`/`MultiplyChecked` overflow guards (`DiagnosticReporter.cpp:49-65`, call sites `:138-155`) on disk-size math that cannot overflow on real inputs — the trust-boundary directive says no defensive validation between our own functions.

## Design

1. **Verify each claim first** (per Diagnosis Discipline). Confirm the duplication byte-for-byte against `RunProcess`, the unbounded `mCarry` growth, and that the overflow guards are unreachable on real inputs. Refuted claims are dropped and named as residuals.
2. Add an optional streaming-sink callback to `toolcli::RunProcess` / `RunProcessOptions` (`Tools/ToolCommon/ToolCliCommon.h:38-55`), invoked per read chunk; default `nullptr` keeps the current buffered `ProcessResult::output` behavior for all existing callers. Delete `LaunchJobProcess` and `CreateOutputPipe` from `BuildCommand.cpp` and route `RunMsBuildToLog` through the shared helper. Preserve BuildCommand's existing guarantees: `bKillOnJobClose` job-kill semantics, exactly one `broken-engine-build-result/v1` JSON object on stdout, failure messages still recorded via `FailBuild`/`sBuildMessages`, and the zero-byte-read semantics — `RunMsBuildToLog` treats `ReadFile` TRUE/0 as continue (`BuildCommand.cpp:395-401`) while `RunProcess`'s loop exits on it (`ToolCliCommon.cpp:167`); reconcile deliberately, not by accident.
3. Trivial extras, same TUs, each independently verified before editing: cap `DiagnosticParser::mCarry` at the existing `kuiMaxDiagnosticLineLength` (drop the line once the carry exceeds the cap; the retained log keeps it verbatim); delete `AddChecked`/`MultiplyChecked` from `DiagnosticReporter.cpp` and inline plain arithmetic at their call sites.

## Critical files

- `Tools/ToolCommon/ToolCliCommon.cpp` / `ToolCliCommon.h` — `RunProcess`, `RunProcessOptions`, `ProcessResult`
- `Tools/WorktreeCli/BuildCommand.cpp` — `LaunchJobProcess`, `CreateOutputPipe`, `RunMsBuildToLog`, `DiagnosticParser`
- `DataPacker/Source/DiagnosticReporter.cpp` — `AddChecked`, `MultiplyChecked`

## Out of scope

- The `broken-engine-build-result/v1` JSON schema, promotion/candidate scripts, and fixture harnesses (`Test-BuildResultFixtures.ps1` runs only if the result contract changes — it should not).
- Any behavior change to build invocation.
- The separate `AddChecked` in `DataPacker/Source/FileManager.cpp:179` (used at `:442`) — same pattern, different TU; note it as a residual if the DiagnosticReporter deletion is confirmed, do not fold it in.

## Acceptance criteria

- Every executed item has a recorded verification result preceding its change.
- No `CreateProcessW` call remains in `BuildCommand.cpp`.
- A WorktreeCli build of an unchanged project produces byte-identical result JSON (modulo timestamps/paths) before and after.
- Both tools (WorktreeCli, DataPacker) compile.

## Notes

- Invariant exposure: tool executables only — no game runtime, no determinism/CRC, wire, `.pack`/`kiVersion`, replay, or allocation-tracked exposure.
- Warning-only overlap: if `Tools/PlanOrderFixtureLongPathFailure.md` or other live Tools plans land mid-flight, refresh citations before executing.
