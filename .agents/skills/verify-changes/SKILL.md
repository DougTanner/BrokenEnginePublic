---
name: verify-changes
description: >-
  Verify final-evidence-gate changes — terminal Plan preparation or release,
  reconciliation, requested primary commit or landing, shared build/bootstrap
  work, or Tier-3 integration — with a read-only final-tree acceptance table.
allowed-tools: [Read, Grep, Glob, "Bash(git diff *)", "Bash(git status *)", "Bash(git ls-files *)", "Bash(git hash-object *)", "Bash(git submodule status *)", PowerShell]
---

# Verify Changes

Run only when the root `AGENTS.md` final-evidence gate applies. Main dispatches
one fresh `reviewer` context separate from implementation and prior review.
That worker is a read-only verifier: do not delegate, edit files, resolve
findings, build, launch a runtime or harness, mutate Plan claims, or create an
evidence artifact. It inspects the final tree and validates supplied evidence;
return missing or stale work to main for adjudication and routing.

## Inputs

Main supplies, and the verifier requires:

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

1. Derive the tracked manifest from `git diff --raw <baseline> --` and append
   `git ls-files --others --exclude-standard`. Preserve status, rename
   source/destination, and untracked identity. This covers committed, staged,
   and unstaged changes from the fixed baseline.
   Record each entry's mode and content identity, `-` for a deletion. Mode is
   part of the identity, so a `100644`→`100755` or symlink↔regular change counts
   even when content is unchanged. Mode and content come from different sources,
   and neither substitutes for the other:
   - Mode: the destination-mode column of `git diff --raw` for a tracked entry,
     which is exact for committed and uncommitted changes alike because mode
     needs no hashing; the filesystem entry type for an untracked one. Do not
     use `git status --porcelain=v2` — it reports only against HEAD/index, so an
     entry changed in an earlier session commit is clean there and yields no
     record at all.
   - Content: route on the mode first, never on whether a command succeeded.
     For a regular file use `git hash-object -- <path>`, the only way to hash
     unstaged bytes — `git diff --raw` zeroes its destination OID against a
     working tree. For a submodule gitlink (`160000`) use
     `git submodule status -- <path>`, whose leading `+`/`-` also carries index
     divergence and uninitialized state. For a symlink (`120000`) use its link
     target. Never invoke `git hash-object` on a gitlink or a symlink: on a
     gitlink and on a directory symlink it dies, but on a file symlink it
     silently succeeds and returns the *target file's* blob, which records a
     plausible wrong identity and hides a retarget to a same-content path.
2. Reconcile every entry against the caller-owned path list and handoffs. Block
   on an unowned entry, an owned path absent from the manifest without an
   explained no-change disposition, a status mismatch, or a handoff claiming
   bytes outside its ownership. Return the manifest inline with both the mode
   and content-identity columns, which `/finalize-changes` requires as inputs;
   no report file is required.
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
and artifact/log path when applicable. Every `PASS` rests on evidence this
verifier itself read, a command it itself ran, or a mandated handoff whose
required fields bear on that row's criterion. A criterion supported only by
narrative assertion, or by evidence that does not bear on it, is
`UNVERIFIED`. One check may cover several rows;
duplicate checks must name their distinct independent signal. Apply the
approved tier as an exploration
ceiling, not permission to weaken a criterion: Tier 1 uses decisive static,
schema, link, validator, and affected-target compile evidence; Tier 2 adds the
smallest approved observable scenario; Tier 3 adds only exposed invariant and
integration evidence. Skill changes require the complete `/validate-skill`
`Validation: PASS` handoff, including self-check, target command/exit/output,
semantic review, and no Critical finding.

Only the user may revise or defer an approved criterion, and only through an
approved delta supplied in the inputs. Build rows from the revised set, with the
row's evidence naming that delta and, for a deferral, the tracked Plan path that
owns the deferred behavior. Without the delta the original criterion stands and
its failure is non-passing; never relax or retire a criterion on your own
authority.

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
- status, process exit code, and failure kind;
- retained-log path and `complete`, relevant structured diagnostics, every
  error and tool message, truncation state, plus relevant changed-file
  warnings; and
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

### Executable Plan validation

For a changed executable plan in a session worktree, use the current checkout's
provisioned `Tools/WorktreeCli/Platforms/VisualStudio2026/Output/WorktreeCli.exe`
and record the read-only session prevalidation from `plan validate --repo
<canonical-common-dir> --worktree <session-worktree> --baseline
<baseline-commit>`. When holding a terminal Plan claim receipt, append
`--terminal-receipt <receipt-path> --terminal-receipt-sha256 <sha256>`; omit
the pair entirely for ordinary no-claim work. Without it a completed Plan
whose Markdown file is deleted returns `status: invalid`, `code:
invalid-plans`, and diagnostic `baseline-plan-missing-or-demoted`. The receipt
must be an ordinary file below `<session-worktree>/Temp`, and the claim state
must be `preparing` or `awaiting-landing` — `Complete-NextPlan.ps1` already
leaves `awaiting-landing` before this skill runs. Require exit `0`, the
versioned JSON contract, `status: valid`, `code: ok`, and no failing
diagnostic. Record stale dependency notices and healed claims. Never validate
`Documents/Features` as scheduler input. Otherwise record `not triggered — no
executable-Plan change`.

Do not represent session prevalidation as primary validation. On the
`primary-commit` route, post-commit primary validation and any receipt-bound
terminal claim release are `/finalize-changes` obligations and cannot be claimed
by this pre-commit audit.

## Decision and Output

The verifier returns `Verification: PASS` only when the manifest reconciles and
every in-scope item passes with current evidence. Any failure, blocker, unverified item,
unresolved claim, unadjudicated refutation, stale evidence, or missing input
returns `Verification: BLOCKED`. Consolidate all such items once; do not retry,
fix, waive, downgrade, or create a follow-up. Main adjudicates and routes
work, then starts a new verification run after any mutation. A `PASS` is scoped
to the manifest it audited; report that manifest and baseline with the table,
and treat any later manifest change as voiding it. Stop when the matrix passes.

Follow `../../references/subagent-reporting.md`
and return: verification result; route, adopted checkout, and baseline; inline
manifest; acceptance, review, and API rows; plan prevalidation; AgentTools
pre-approval obligation when applicable; fix/re-entry history or `none`;
non-passing items or `none`; and `Residuals: <blocker or none>` last.
