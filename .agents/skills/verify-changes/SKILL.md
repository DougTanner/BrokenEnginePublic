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
findings, build, launch a runtime or harness, invoke Plan mutation commands
(`claim-next`, `unclaim`, terminal preparation, or release), alter the active
deterministic receipt, alter the tracked tree, or create an evidence artifact.
Canonical `plan validate` may perform its established stale machine-local claim
healing; that is the sole permitted scheduler side effect and is not
side-effect-free. It inspects the final tree and validates supplied evidence;
return missing or stale work to main for adjudication and routing.

## Inputs

Main supplies, and the verifier requires:

- absolute adopted checkout, fixed session-start baseline commit, and route:
  `session-finalization` or explicitly authorized `primary-commit`;
- exact reconciled candidate commit and expected candidate tree, created after
  terminal Plan preparation and reconciliation/single-parent squash;
- final approved plan and approved deltas (`none` is valid);
- execution-control record with tier/triggers, required/conditional roles and
  dispositions, initial acceptance matrix, and every declared invariant;
- caller-owned paths plus complete implementation, propagation, check, review,
  hygiene, build, external-claim, fix, and residual handoffs in execution order.

Do not rediscover a movable baseline, invent a criterion, or accept an
unapproved delta. Require the adopted path to equal its Git top level, the
baseline to resolve exactly, and primary checkout use only on the
`primary-commit` route.

## Candidate identity and inventory

Required order: terminal preparation -> candidate creation -> reconciliation/single-parent squash -> exact candidate verification -> finalization summary and explicit confirmation -> primary mutation.

1. Resolve the supplied candidate with `git rev-parse --verify
   <candidate>^{commit}` and its tree with `git rev-parse <candidate>^{tree}`.
   Require exact equality to the supplied commit and tree and exactly one parent.
   On a session route, require the adopted-checkout `HEAD` to equal the candidate
   and its sole parent to be the reconciled primary tip recorded by the
   finalization handoff. On a primary-commit route, require the candidate parent
   and adopted-checkout `HEAD` to equal the current primary tip without advancing
   that ref. The fixed baseline
   must still resolve exactly and the reconciliation record must account for any
   primary advance from it. Missing, non-commit, wrong-parent, wrong-tree, or
   changed-tip identity is a blocker, never an evidence waiver.
2. Derive the reviewed inventory directly from Git: use `git diff --raw -M
   <candidate>^ <candidate>` and the corresponding name-status output, retaining
   paths, modes, additions, deletions, renames, symlinks, and gitlinks. The
   candidate is clean by construction, so do not mix index/worktree state or
   manufacture per-entry content identities. Derive the fixed-baseline delta
   separately when required to account for the complete session history; the
   candidate-parent delta is the exact scope being reviewed and landed.
3. Reconcile the derived changed-path set exactly against the declared
   caller-owned paths UNION receipt-proven terminal `changedPaths`. Block on an
   unowned candidate path, an authorized changed path absent from the candidate
   without an explained no-change disposition, or a handoff claiming bytes
   outside that union. Return the Git-derived inventory inline with the baseline,
   candidate commit, and candidate tree; no synthetic identity envelope is an
   input to `/finalize-changes`.
4. A caller fix ends the current run. It creates and reconciles a replacement
   candidate before re-entry; recompute the Git inventory, add the fix handoff,
   and invalidate evidence affected by the changed candidate delta. Require the
   handoff sequence to prove no later candidate replacement invalidated each
   accepted check, review, build, receipt, or data snapshot.
5. Inventory touched ignored or non-worktree state from the handoffs. Require
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
  disposition, and each exact `broken-engine-data-oracle/v1` receipt path,
  SHA-256, Data path, mode, baseline, and aggregate digest. Require a current
  passing `Test-DataOracleReceipt.ps1` result against every selected receipt;
  Local mode also requires the independent primary Shared receipt. Never infer
  a receipt identity, accept a path/mode/baseline mismatch, or require Shared
  and Local receipts to equal.

Require schema/result/exit consistency, complete retained log, intended target
and selection, all requested targets, oracle consistency across consumers,
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

For a changed executable Plan in a session worktree, invoke
`scripts/Test-VerifyPlanPrevalidation.ps1 -Worktree <absolute-session-worktree>
-Baseline <baseline-commit>`. It derives wrapper provenance and the provisioned
WorktreeCli, discovers any deterministic Plan receipt internally, and is the
only verification-stage Plan prevalidation route. Require exit `0`, `status:
pass`, `code: ok`, and `validation.status: valid`; record its safe claim state
and terminal proof. On validation failure it returns top-level `status:
blocked`, `code: validation.failed`, with WorktreeCli detail in
`validation.diagnostics`. An absent receipt uses ordinary validation. A present
`claimed` receipt proves its Plan bytes then also uses ordinary validation; a
present `awaiting-landing` receipt with disposition `completed` or `rejected`
uses the internal terminal proof. A `preparing`, invalid, or unowned present
receipt blocks. Record stale dependency notices and healed claims. Never validate
`Documents/Features` as scheduler input. Otherwise record `not triggered — no
executable-Plan change`.

Do not represent session prevalidation as primary validation. On the
`primary-commit` route, post-commit primary validation and any receipt-bound
terminal claim release are `/finalize-changes` obligations and cannot be claimed
by this pre-commit audit.

## Decision and Output

The verifier returns `Verification: PASS` only when the candidate identity and
ownership reconciliation pass and every in-scope item has current evidence. Any
failure, blocker, unverified item, unresolved claim, unadjudicated refutation,
stale evidence, or missing input returns `Verification: BLOCKED`. Consolidate
all such items once; do not retry, fix, waive, downgrade, or create a follow-up.
Main adjudicates and routes work, then starts a new verification run after any
candidate replacement. A `PASS` is bound to its fixed baseline and exact
candidate commit/tree; any candidate, parent, tree, or tip change voids it.
Stop when the matrix passes.

Follow `../../references/subagent-reporting.md` and return: verification result;
route, adopted checkout, fixed baseline, candidate commit/tree, sole parent,
and Git-derived changed-file inventory; acceptance, review, and API rows; plan
prevalidation; AgentTools pre-approval obligation when applicable; fix/re-entry
history or `none`; non-passing items or `none`; and `Residuals: <blocker or
none>` last.
