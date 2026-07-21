---
name: verify-changes
description: >-
  Verify final-evidence-gate changes — queue mutation or completion,
  reconciliation, requested primary commit or landing, shared build/bootstrap
  work, or Tier-3 integration — with a read-only final-tree acceptance table.
allowed-tools: [Read, Grep, Glob, "Bash(git diff *)", "Bash(git status *)", "Bash(git ls-files *)", PowerShell]
---

# Verify Changes

Run only when the root `AGENTS.md` final-evidence gate applies. Use a fresh
context separate from implementation and review. This is a read-only verifier:
do not delegate, edit files, resolve findings, build, launch a runtime or
harness, mutate the queue, or create an evidence artifact. Inspect the final
tree and validate supplied evidence; return missing or stale work to the caller.

## Inputs

Require:

- absolute adopted checkout, fixed session-start baseline commit, and route:
  `session-finalization` or explicitly authorized `primary-commit`;
- final approved plan and approved deltas (`none` is valid);
- execution-control record with tier/triggers, required/conditional roles and
  dispositions, initial acceptance matrix, and every declared invariant;
- caller-owned paths plus complete implementation, propagation, check, review,
  hygiene, build, external-claim, fix, and residual handoffs in execution order.

Do not rediscover a movable baseline, invent a criterion, or accept an
unapproved delta. Require the adopted path to equal its Git top level, the
baseline to resolve exactly, and primary checkout use only on the
`primary-commit` route.

## Final Manifest

1. Derive the tracked manifest from `git diff --name-status <baseline> --` and
   append `git ls-files --others --exclude-standard`. Preserve status, rename
   source/destination, and untracked identity. This covers committed, staged,
   and unstaged changes from the fixed baseline.
2. Reconcile every entry against the caller-owned path list and handoffs. Block
   on an unowned entry, an owned path absent from the manifest without an
   explained no-change disposition, a status mismatch, or a handoff claiming
   bytes outside its ownership. Return the manifest inline; no hashes or report
   file are required.
3. A caller fix ends the current run. On re-entry, recompute and reconcile the
   whole manifest, add the fix handoff, and invalidate only evidence affected by
   the new bytes. Require the handoff sequence to prove no later manifest edit
   invalidated each accepted check, review, build, receipt, or data snapshot.
4. Inventory touched ignored or non-worktree state from the handoffs. Require
   its exact path, owner contract or serialization mechanism, evidence, and one
   disposition: `unchanged`, `intentionally persisted`, `restored`, or
   `residual`. A residual is non-passing.

## Acceptance Audit

Build one inline row per approved criterion, invariant, visible changed
behavior, required test/review, and handed-off residual. Use:

`criterion | decisive check | status | evidence`

Statuses are `PASS`, `FAIL`, `BLOCKED`, or `UNVERIFIED`; never use `SKIPPED`.
Evidence names the exact command/scenario, exit or verdict, decisive output,
and artifact/log path when applicable. One check may cover several rows;
duplicate checks must name their distinct independent signal. Apply the
approved tier as an exploration
ceiling, not permission to weaken a criterion: Tier 1 uses decisive static,
schema, link, validator, and affected-target compile evidence; Tier 2 adds the
smallest approved observable scenario; Tier 3 adds only exposed invariant and
integration evidence. Skill changes require the complete `/validate-skill`
`Validation: PASS` handoff, including self-check, target command/exit/output,
semantic review, and no Critical finding.

For each required review, add:

`review | delegated/inline | findings | accepted-fixed | refuted | unresolved | evidence`

Require the counts to reconcile, every accepted finding to have fix and
recheck evidence, and zero unresolved accepted findings. Do not rerun reviews.

For each external/API proposition, add:

`proposition | version/configuration | official source | verdict | dependent finding | evidence`

Keep the proposition stable and atomic. Accept `VERIFIED` only with an
applicable primary official source. Accept `REFUTED` only when the handoffs
include the manager's explicit disposition of its dependent finding.
`UNRESOLVED` and unadjudicated refutations are non-passing. Do not silently
discard or rewrite a proposition, verdict, or dependent finding; a changed
proposition is a new request.

### Build evidence

For every requested build, require one authoritative
`broken-engine-build-result/v1` record and expose:

- target and worktree root, exact arguments, selected files, and invalidated
  objects;
- status, process exit code, failure kind, lock result, MSBuild discovery,
  launch, and exit;
- retained-log path and `complete`, relevant structured diagnostics, every
  error and tool message, truncation state, plus relevant changed-file warnings;
- started time and elapsed time; and
- for game builds, data mode and trigger, `RunDataPacker`, canonical data and
  generated-include paths, prepared-data or generation authority, Gaea-guard
  disposition, selected-data snapshot, and primary snapshot when Local.

Require schema/result/exit consistency, complete retained log, intended target
and selection, all requested targets, snapshot consistency across consumers,
and no later manifest edit that invalidates the build. Terminal text is not a
substitute. A truncated diagnostic set requires the retained log.

For a non-Markdown AgentTools change, also require successful candidate
production, a `broken-engine-agenttools-candidate/v2` receipt with recorded
path and SHA-256, identical before/after source manifests, current-tree
membership/byte match, immutable executable hash match, and capability result.
For an already committed candidate, record read-only
`broken-engine-agenttools-certification-result/v1` `status: pass`, `code: ok`,
and exact expected-commit/tree match. An ordinary session or `primary-commit`
prevalidation cannot certify its uncommitted tree: record `/finalize-changes`
certification against the reconciled candidate as a mandatory pre-approval
obligation, never as evidence already passed. Any receipt, certification, or
tree mismatch requires a rebuilt candidate.

### Executable-plan validation

For a changed executable plan in a session worktree, use the current checkout's
provisioned `Tools/WorktreeCli/Platforms/VisualStudio2026/Output/WorktreeCli.exe`
and record the read-only session prevalidation from `plan order validate --repo
<canonical-common-dir> --worktree <session-worktree>`. Require exit `0`, `ok:
true`, and no failing diagnostic. An `ok: true` foreign-row `missing-plan-file`
stale-baseline notice is evidence, not failure; an owner-held completed row is
expected until finalization. Otherwise record
`not triggered — no executable-plan change`.

Do not represent session prevalidation as primary validation. On the
`primary-commit` route, post-commit primary validation and any owner-unclaim are
`/finalize-changes` obligations and cannot be claimed by this pre-commit audit.

## Decision and Output

Return `Verification: PASS` only when the manifest reconciles and every in-scope
item passes with current evidence. Any failure, blocker, unverified item,
unresolved claim, unadjudicated refutation, stale evidence, or missing input
returns `Verification: BLOCKED`. Consolidate all such items once; do not retry,
fix, waive, downgrade, or create a follow-up. The caller adjudicates and routes
work, then starts a new verification run after any mutation. Stop when the
matrix passes.

Follow [`../../references/subagent-reporting.md`](../../references/subagent-reporting.md)
and return: verification result; route, adopted checkout, and baseline; inline
manifest; acceptance, review, and API rows; plan prevalidation; AgentTools
pre-approval obligation when applicable; fix/re-entry history or `none`;
non-passing items or `none`; and `Residuals: <blocker or none>` last.
