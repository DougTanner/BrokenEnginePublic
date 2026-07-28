<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-27T00:07:00.951Z","dependsOn":[]} -->
# Landing must validate Plans against the primary tip, not the session receipt baseline

## Context

`Invoke-FinalizeLanding.ps1` validates the reconciled Plan tree against the wrapper session's *identity* baseline instead of the commit primary actually contains. When those two commits differ and the claimed Plan's bytes changed between them, a correct, fully approved landing is refused and cannot proceed.

Three sites produce the defect together:

- `.agents/skills/finalize-changes/scripts/Test-FinalizePreflight.ps1:355` — `if ($receipt.baseline -cne $Baseline) { Stop-Validation 'receipt.baseline-mismatch' ... }`. The supplied `-Baseline` is forced to equal the in-worktree wrapper receipt's `baseline` field. That field is written once at session start and is a *session-lineage* value: it exists so a reattach proof can verify the worktree descends from a known commit.
- `.agents/skills/finalize-changes/scripts/Invoke-FinalizeLanding.ps1:138-139` — `Assert-ReconciledPlanMetadata` forwards that same `$Baseline` verbatim to `plan validate` as `--baseline`, where it is no longer a lineage anchor but the *content* baseline the reconciled tree is diffed against.
- `Tools/WorktreeCli/PlanScheduler.cpp:1063` — `if (!HasTerminalReceiptFor(rArguments, rRepository, rWorktree, path, baselinePlan.digest))`. The terminal receipt is accepted only when it matches the plan's digest **at the validation baseline**. The receipt is keyed to the plan bytes at claim time (`primaryCommit` in the claim receipt).

So when primary edits the claimed Plan after the wrapper baseline is pinned, the receipt attests the claim-time bytes while validation demands the older baseline bytes, and `ValidateBaselineMetadata` emits `baseline-plan-missing-or-demoted` — "baseline executable plan was removed or demoted without its terminal receipt" — for a plan whose removal *is* correctly receipted. The landing fails at `plan.validation-failed` before primary is touched. Nothing in `/finalize-changes` reconciles the two commits for a non-squash advance: `Repair-AgentWorktreeSquashedBaseline.ps1` only handles a squash rewrite, and it does not fire while the old baseline is still an ancestor of the primary tip.

Observed on 2026-07-26, landing session branch `claude/4a2a1c15-78f8-4827-8bb3-c77b631233a2` onto `2.0.0`. Wrapper receipt baseline `1e45be94`; primary tip `13099512`; claim receipt `primaryCommit` `0949f6d2`. Primary commit `d7bb83f5` ("Enrol transcript finder plan in scheduler") had edited the claimed Plan in between:

| commit | role | blob of the completed mandatory-executable-plan-metadata Plan |
|---|---|---|
| `1e45be94` | wrapper receipt baseline | `c0acfec2623f5848c8b0df6587502af8117bb19e` |
| `0949f6d2` | claim receipt `primaryCommit` | `6345ca1b97f9b26621bad5f61a005c7dbc232424` |
| `13099512` | primary tip at landing | `6345ca1b97f9b26621bad5f61a005c7dbc232424` |

The binary is irrelevant — only the baseline decides. The same `plan validate` invocation, same tree, same terminal receipt, run four ways:

| binary | `--baseline 1e45be94` (receipt) | `--baseline 13099512` (primary tip) |
|---|---|---|
| canonical primary `WorktreeCli.exe` | `invalid` / `invalid-plans`, 1 diagnostic | `valid` / `ok`, 0 diagnostics |
| session candidate `WorktreeCli.exe` | `invalid` / `invalid-plans`, 1 diagnostic | `valid` / `ok`, 0 diagnostics |

At the receipt baseline the run also emits two `missing-plan-file` notices for Plans that primary itself deleted; both disappear at the primary tip. Those notices are a second symptom of the same stale input, not independent findings.

That session unblocked itself by advancing the wrapper receipt `baseline` to the primary tip through `Update-AgentWorktreeSessionReceiptBaseline`, under explicit user authorization, because `AgentWorktreeSession.psm1:150-154` reserves that rewrite for squash re-parenting. That was a one-off manual workaround, not the fix: it required user authority for a mechanical condition and it mutates the session trust anchor to satisfy a content check that should never have consulted it.

Originating gap: pre-existing in the finalization sidecars, outside the boundary of the change that hit it (a Plans-metadata validation rule). Not an acceptance failure of that change.

## Design

Separate the two meanings of "baseline". Keep `-Baseline` as the session-identity value the preflight checks against the receipt; give `plan validate` the commit primary actually contains.

- In `Invoke-FinalizeLanding.ps1:138`, pass `$script:PrimaryIdentity.Head` as `--baseline` instead of `$Baseline`. That variable is already resolved at `:243` and recorded at `:253`, both well before `Assert-ReconciledPlanMetadata` is called at `:300`, so no new Git read is added.
- The value is already proven correct at that point: the `pre-mutation` preflight at `:272` fails with `git.primary-tip-changed` if the actual primary tip differs from `ExpectedPrimaryTip`, and `Test-FinalizePreflight.ps1:267` proves the primary tip is an ancestor of the reconciled session tip. The primary tip is therefore exactly "what primary contained before this landing" — the content baseline `plan validate` is defined against.
- Leave `Test-FinalizePreflight.ps1:355` alone. Binding `-Baseline` to the receipt is the correct session-identity check; it is only its reuse as a content baseline that is wrong.
- Leave `HasTerminalReceiptFor` and `ValidateBaselineMetadata` alone. Given the right baseline they already behave correctly, as the matrix above shows.

Rejected alternative: teaching `HasTerminalReceiptFor` to also accept a receipt matching the plan at the claim's `primaryCommit`. It widens the trust surface of the terminal-receipt proof to fix a caller passing the wrong argument, and still leaves the stale baseline distorting every other baseline comparison in the same run.

Rejected alternative: a sanctioned receipt re-baseline inside finalization for a non-squash advance. It writes to the session trust anchor on a routine path, and the receipt baseline is not what `plan validate` should consult in the first place.

## Critical files

- `.agents/skills/finalize-changes/scripts/Invoke-FinalizeLanding.ps1` — `Assert-ReconciledPlanMetadata:137-152` only, and only the `--baseline` argument at `:138`. `$script:PrimaryIdentity` (`:243`), `$result.tips.primary` (`:253`), and the call site (`:300`) are read-only references.
- `.agents/skills/finalize-changes/scripts/Test-FinalizePreflight.ps1` — read-only. Confirm `:355` still binds `-Baseline` to the receipt and `:267` still proves the primary-tip ancestry the design depends on.
- `.agents/skills/finalize-changes/scripts/Test-FinalizeWorkflowFixtures.ps1` — session-landing and terminal-release coverage; add the drift case named in Acceptance criteria.
- `.agents/skills/finalize-changes/SKILL.md` — only if the Inputs or step 5 wording states which commit the landing validates against.

## Out of scope

- The wrapper receipt's `baseline` field, its immutability, `Update-AgentWorktreeSessionReceiptBaseline`, and `Repair-AgentWorktreeSquashedBaseline.ps1`. The squash re-parent path keeps its current sole ownership of that rewrite.
- `Tools/WorktreeCli/PlanScheduler.cpp` — `HasTerminalReceiptFor`, `ValidateBaselineMetadata`, the `baseline-plan-missing-or-demoted` diagnostic, and the `missing-plan-file` notice all keep identical semantics.
- The claim receipt schema, `primaryCommit`, `reparent-claims`, and claim lifecycle.
- Which commit `Invoke-FinalizeApprovalPreparation.ps1` or the preflight checkpoints receive as `-Baseline`.
- The `primary-commit` mode, which has no session receipt and no reconciliation.
- Adding a unit test or a new test project.

## Risk tier

Tier 3. Trigger: changes `Invoke-FinalizeLanding.ps1`, the coordination sidecar every wrapper session's `/finalize-changes` invokes under the PC-global landing lock to mutate primary — coordination that can block other sessions.

Invariants that must hold:

- A landing that validates clean today still validates clean. When the receipt baseline and the primary tip are the same commit — the common case — the argument is identical and behaviour is unchanged.
- Validation still refuses a genuinely unproven terminal deletion: a removed baseline Plan with no matching terminal receipt at the primary tip must still produce `baseline-plan-missing-or-demoted` and still block before any primary mutation.
- No new Git or WorktreeCli invocation, no change to the landing's ordering, lock ownership, or exit codes.
- Primary is still untouched on any validation failure.

No determinism/CRC, wire protocol, serialization, save/replay, threading, allocation-tracked runtime, or shader exposure.

## Acceptance criteria

- A session whose claimed Plan was edited on primary after the wrapper baseline was pinned lands without any receipt rewrite and without user authorization: `plan validate` inside the landing returns `status: valid`, `code: ok`, 0 diagnostics, and the landing reports `status: landed`.
- The same session's preflight still binds `-Baseline` to the receipt value and still rejects a mismatched one with `receipt.baseline-mismatch`.
- A landing whose terminal Plan is removed with no valid terminal receipt still fails with `plan.validation-failed` carrying `baseline-plan-missing-or-demoted`, and primary is unchanged afterwards.
- A landing where the receipt baseline equals the primary tip produces byte-identical `plan validate` arguments to today's.
- Existing `Test-FinalizeWorkflowFixtures.ps1` cases pass unmodified.

## Coordination

`Documents/Plans/Tools/TerminalReleaseConflictAttribution.md` changes what `RunReleaseAfterLanding` reports in `PlanScheduler.cpp` and treats `Invoke-FinalizeLanding.ps1` as read-only. This Plan changes one argument in `Assert-ReconciledPlanMetadata` and treats `PlanScheduler.cpp` as read-only. Disjoint boundaries, no ordering requirement, no metadata edge.

`Documents/Plans/Tools/TerminalPreparationChildSnapshotConsistency.md` works in `RunPrepare` and the terminal-preparation fixture block. This Plan touches neither. Whichever lands second re-cites `Test-FinalizeWorkflowFixtures.ps1` line numbers.

## Verification

1. Run `.agents/skills/finalize-changes/scripts/Test-FinalizeWorkflowFixtures.ps1`; all pre-existing cases plus the new baseline-drift case pass.
2. Construct the drift case in the fixture's scratch repository — claim a Plan, advance primary so the claimed Plan's bytes change, reconcile, then land — and assert it lands with the receipt baseline left untouched.
