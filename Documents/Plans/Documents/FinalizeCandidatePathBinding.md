<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-02T01:11:50.589Z","dependsOn":[]} -->
# Finalize Candidate Commit Multi-Path Binding

## Context

`.agents/skills/finalize-changes/scripts/Invoke-FinalizeCandidateCommit.ps1:13` declares `[Parameter(Mandatory)][string[]] $OwnedPaths`. `pwsh -File` hands every argument over as one literal string, so a comma-separated path list binds as a single one-element array containing the whole joined string instead of one element per path.

Observed during the landing of `c7a85890` (Codex session `019fbeb5-453b-7180-8c64-db2cbc2a19a6`, finalizer child, 20:21:43Z): the first candidate pass accepted only the first path of a three-path change; the finalizer caught the truncated candidate only by re-inspecting the tree and retried with a corrected invocation. Uncaught, this silently lands a partial commit.

The repository already documents the same `pwsh -File` constraint for `.agents/scripts/New-PlanFile.ps1 -DependsOn`, whose contract is a single comma-separated token the script splits itself (`.agents/references/new-plan-file.md`).

## Design

Make the script split a single comma-joined `$OwnedPaths` element into individual paths before validation, mirroring the `New-PlanFile.ps1 -DependsOn` convention, and document the comma-separated single-token form as the `pwsh -File` invocation shape in the finalize-changes script reference. A genuine array passed by a PowerShell caller binds exactly as today. Reject an empty element after splitting with the existing invalid-path outcome.

## Critical files

- `.agents/skills/finalize-changes/scripts/Invoke-FinalizeCandidateCommit.ps1` — `$OwnedPaths` binding and the path-validation entry (`Normalize-Path` callers).
- `.agents/skills/finalize-changes/references/scripts.md` — documented invocation form.
- `.agents/scripts/New-PlanFile.ps1` — read-only convention reference.

## In scope

- `Invoke-FinalizeCandidateCommit.ps1`: split a single comma-containing `$OwnedPaths` element into the path list before existing validation; no other parameter or behavior change.
- `references/scripts.md`: the one invocation-form sentence.

## Out of scope

- Any other finalize-changes script, the candidate projection logic, commit construction, or landing behavior.
- Commas inside real path names (the repository's path validation already rejects such paths).
- Any change to result schemas or exit codes.

## Risk tier and invariants

Tier 2 — scoped tool behavior in one landing-support script; a defect here silently truncates a landing candidate, but the change itself adds no coordination or invariant surface.

Invariants: every authorized path supplied to the script reaches the candidate; array invocations bind byte-identically to today; validation still rejects malformed paths.

## Acceptance criteria

- A `pwsh -File` invocation passing `'a.md,b.md,c.md'` produces a candidate containing all three paths.
- A native array invocation over the same fixture produces an identical candidate.
- `Test-FinalizeWorkflowFixtures.ps1` passes.
