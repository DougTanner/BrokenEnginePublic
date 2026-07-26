<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-26T18:19:35.568Z","dependsOn":[]} -->
# Terminal Manifest Reconciliation Tolerance

## Context

`plan prepare-completion` refuses to land a legitimately reconciled Plan file, and no routed recovery exists for that refusal.

Observed on landed commit `32cba814316dc42144a72f93258afc81b162043f`. A session claimed terminal Plan `Documents/Plans/Tools/Architecture_HarnessLockIntegrity.md` and ran `plan prepare-completion`, which recorded whole-file SHA-256 digests of the target and its direct child `Documents/Plans/Tools/Refactor_AgentHarnessCommandBoundary.md` into the machine-local terminal manifest. Primary had already advanced (commit `8c77df70`), independently editing that child. The reconciliation rebase correctly combined primary's edits with the session's dependency removal, but the merged file's whole-file hash matched neither recorded digest, so `RunPrepare` returned `recovery-conflict: child plan has third-party bytes` and the landing was refused. The change was already implemented, reviewed, and verified; only the guard was wrong. The landing completed only after an explicit user-authorized override.

Root cause is a scope mismatch, not a hashing defect. `RenderDependencies` (`Tools/WorktreeCli/PlanScheduler.cpp:1456-1474`) rewrites **only** the byte-zero marker's JSON payload and copies the entire document body verbatim (`rBytes = kMarkerPrefix + metadata.dump() + kMarkerSuffix + rPlan.bytes.substr(uiSuffixStart)`). The transaction owns the marker; the manifest guards the whole file. Any legitimate edit to bytes the transaction never touches trips the guard.

Ordering is not the bug and must not be changed. `.agents/skills/next-plan/references/execution-gates.md:46-49` mandates implementation → verification → terminal preparation → reconciliation → finalization, so terminal preparation is *required* to run before the reconciliation rebase. A stale primary at preparation time is the normal expected state. Making preparation refuse a stale baseline would block the documented flow on every mid-session primary advance.

Two supporting gaps, both confirmed by search:

- **No routed recovery.** The string `recovery-conflict` appears only in `Tools/WorktreeCli/PlanScheduler.cpp`; there are zero occurrences across `.agents/skills/**` and `Documents/**`. `Invoke-FinalizeLanding.ps1:284-286` has no branch for it and collapses it into a generic `plan.prepare-failed` blocker, so an agent hitting it has no routed next step.
- **No shared-artifact disclosure.** That landing replaced the canonical `AgentHarness.exe` used by every linked worktree and made `--owner` mandatory on socket commands — a breaking CLI contract change. `execution-gates.md:99-116` defines AgentTools promotion as a canonical shared artifact and gates it mechanically, but the landing summary and confirmation templates carry no clause telling the user a shared binary is being replaced.

No fixture drives either third-party-bytes branch (`PlanScheduler.cpp:1633`, `:1657`), which is why this reached production.

## Design

### 1. Classify manifest children by dependency state, not whole-file digest

In `Tools/WorktreeCli/PlanScheduler.cpp`, function `RunPrepare`, child reconciliation loop at `:1606-1645`.

`BuildPlans` already parses `dependencies` for every on-disk plan (`ParsePlanBytes:370-379`), and the loop already holds that record as `found->second` (`:1614`). Use it directly — no new parsing, no new file I/O, no new helper.

Replace the digest classification with:

- On-disk `found->second.dependencies` **does not contain** `target` → already in the after-state. `continue`, pushing to `changed` only when `bRecoveringPreparation`, exactly as the existing `afterSha256` branch does at `:1626-1630`.
- On-disk `found->second.dependencies` **contains** `target` → the rewrite is still pending. Call `RenderDependencies(found->second, target, after)`, write with `coordination::WriteBytesAtomic(diskPath, after)`, push the path to `changed`.

Delete the `recovery-conflict: child plan has third-party bytes` return at `:1631-1634`, and delete the post-render whole-file equality check `coordination::HashSha256(after) != child.value("afterSha256", "")` at `:1636` — it compares the re-rendered file against a stale recorded hash and fails on a merged body for the same reason.

Retain unchanged: the `child-invalid` conflict for an untracked or invalid child (`:1614-1618`), and `recovery-conflict: child plan is absent during terminal recovery` (`:1621-1624`).

Continue writing `beforeSha256` and `afterSha256` into the manifest at `:1539` and `:1592`. `ValidateManifest:519` requires both to be 64-character lower hex, so they remain part of the claim schema and remain useful diagnostics; they simply stop gating the decision.

### 2. Keep the target check strict

Do **not** modify the target digest check at `:1655-1658`. Deletion destroys the whole file, so "did a third party change the plan being deleted?" is a question worth preserving. It does not fire in the reconciliation scenario: the session's own commit deletes the target, so after rebase `ReadBytes` fails at `:1649` and control reaches the already-deleted branch at `:1665-1672`.

### 3. Name the conflict at the landing boundary

In `.agents/skills/finalize-changes/scripts/Invoke-FinalizeLanding.ps1`, the pre-landing terminal preparation block at `:278-294`.

At the failure branch `:284-286`, add a distinct case for a response whose `code` is `recovery-conflict`: report a blocked result naming the conflicting Plan path from the response and pointing at the recovery rule added in change 4. Preserve the existing exit-code mapping (WorktreeCli exit `2` → `Throw-Landing 2`, otherwise `1`), the single-attempt behaviour, and the surrounding `try`/`catch`/`finally` unwind. Add no retry loop.

Leave the `approval.refresh-required` check at `:291-293` unchanged. In the post-rebase case the child is already in its after-state and the claim state is `awaiting-landing` (not `preparing`), so nothing is pushed to `changed` and `changedPaths` stays empty.

### 4. Route the recovery in the contracts

- `.agents/skills/next-plan/references/execution-gates.md` — in the existing "Primary advance" subsection at `:59-67`, add a terminal preparation conflict rule: reconciliation-produced Plan bytes are expected and no longer conflict; a surviving `recovery-conflict` means the terminal target itself changed and needs user judgement, not an override.
- `.agents/skills/finalize-changes/SKILL.md:97` — in the post-landing terminal release paragraph, reference that rule.

### 5. Disclose shared-artifact landings

- `.agents/skills/finalize-changes/SKILL.md:90-94` — the landing summary template gains a required clause, emitted exactly when step 5b promotion applies (the landed diff touches any non-Markdown path under `Tools/WorktreeCli/`, `Tools/AgentHarness/`, or `Tools/ToolCommon/`): state that landing replaces the canonical AgentTools binaries used by all linked worktrees, and name any changed CLI contract.
- `.agents/skills/next-plan/references/execution-gates.md:135-141` — mirror the clause in the confirmation template so both routes carry it.

Disclosure only. The quiescence gating, rollback, and receipt behaviour at `execution-gates.md:99-116` is unchanged.

### 6. Cover the reconciliation case in the existing fixture

Extend `.agents/scripts/Test-WorktreeCliPlanScheduler.ps1` in its terminal-preparation block at `:217-247`, after the existing awaiting-landing retry assertions at `:225-228`. This is an existing integration fixture; do not create a new test project or unit-test framework.

## Critical files

- `Tools/WorktreeCli/PlanScheduler.cpp`
- `Tools/WorktreeCli/AGENTS.md`
- `.agents/skills/finalize-changes/scripts/Invoke-FinalizeLanding.ps1`
- `.agents/skills/finalize-changes/SKILL.md`
- `.agents/skills/next-plan/references/execution-gates.md`
- `.agents/scripts/Test-WorktreeCliPlanScheduler.ps1`

## Scope contract

The listed scope is both target and ceiling. Make the smallest complete change satisfying the acceptance criteria; add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants no permission to touch anything in it beyond the named regions plus the mechanical necessities (includes, declarations) the named change requires.

**In scope — named regions only:**

- `Tools/WorktreeCli/PlanScheduler.cpp`: `RunPrepare`, child reconciliation loop `:1606-1645` only. Anonymous-namespace helpers only if a small predicate shared by that loop is extracted.
- `Tools/WorktreeCli/AGENTS.md`: only the "Coordination State" sentence describing terminal preparation, which documents the replaced comparison.
- `.agents/skills/finalize-changes/scripts/Invoke-FinalizeLanding.ps1`: the failure branch at `:284-286` inside the `:278-294` preparation block only.
- `.agents/skills/finalize-changes/SKILL.md`: the landing summary template at `:90-94` and the terminal release paragraph at `:97` only.
- `.agents/skills/next-plan/references/execution-gates.md`: the "Primary advance" subsection at `:59-67` and the confirmation template at `:135-141` only.
- `.agents/scripts/Test-WorktreeCliPlanScheduler.ps1`: the terminal-preparation block at `:217-247` only, plus its self-reported case list at `:493`.

**Out of scope:**

- The target digest check at `PlanScheduler.cpp:1655-1658`, the manifest creation blocks at `:1510-1545` and `:1566-1605`, `ValidateManifest`, `RenderDependencies`, `ParsePlanBytes`, `BuildPlans`, and the `Plan` struct.
- Reordering terminal preparation relative to reconciliation, and any stale-baseline or primary-tip precondition in `RunPrepare`. `RunPrepare` must not begin consulting the primary tip.
- Scheduler selection order, claim lifecycle, `claim-next`, `release-after-landing`, `dependsOn` semantics, the `broken-engine-plan/v1` marker format, and the claim record schema.
- The quiescence, rollback, and receipt machinery behind AgentTools promotion; `Invoke-AgentToolsPromotion.ps1`.
- Removing the terminal manifest entirely. It is arguably redundant with the receipt-bound `release-after-landing` proof, but removing a safety control requires evidence it never fires protectively, which does not exist yet.
- Retry loops or automatic override paths for any surviving conflict.

## Coordination

`Documents/Plans/Tools/ReducePlanScheduler.md` is a separate live executable Plan that extracts `ParsePlanBytes`, `BuildPlans`, and the `Plan` struct from `PlanScheduler.cpp` into a new `PlanMetadata.cpp`/`.h` pair. Root cause and implementation boundary differ (file size versus comparison scope) and `RunPrepare` stays in `PlanScheduler.cpp` under both, so neither plan is a prerequisite of the other and no metadata dependency is declared. Whichever lands second adjusts only the include for the moved `Plan` declaration; that adjustment is inside the already-in-scope `PlanScheduler.cpp` region and obliges no additional file.

## Risk tier

Tier 3 — changes `plan prepare-completion`, which every session's `/next-plan` completion and every `/finalize-changes` landing invokes, and relaxes a guard on Plan claim mutation. Requires an execution card, `/plan-audit`, `/external-grill-plan`, `/adversarial-review`, and the final-evidence gate.

Invariant that must hold: a completion must never land while any direct child's marker still lists the terminal target in `dependsOn`. The new classification tests that property directly, whereas the whole-file digest conflated it with unrelated body edits.

No determinism/CRC, wire protocol, serialization, save/replay, threading, or allocation-tracked runtime exposure.

## Acceptance criteria

- After a successful `prepare-completion`, editing a manifest child's document body while leaving its byte-zero marker intact and re-running `prepare-completion` returns `status: ok` with `code: recovered`, and the body edit is preserved on disk.
- Restoring a manifest child's `dependsOn` edge to the terminal target on disk and re-running `prepare-completion` reapplies the marker rewrite and reports that child in `changedPaths`.
- Editing the terminal target's body before its deletion and re-running `prepare-completion` still returns `recovery-conflict` with message `target plan has third-party bytes`.
- A child that is absent, untracked, or invalid still produces its existing conflict code and message.
- `prepare-completion` on an unchanged `awaiting-landing` claim still returns `code: recovered` with an empty `changedPaths`, and on a `preparing` claim still reports the already-applied paths — the existing assertions at `Test-WorktreeCliPlanScheduler.ps1:225-228` and `:230-236` pass unmodified.
- A `recovery-conflict` from pre-landing preparation produces a blocked landing result naming the conflicting Plan path, not a bare `plan.prepare-failed`.
- A landing whose diff touches a non-Markdown path under `Tools/WorktreeCli/`, `Tools/AgentHarness/`, or `Tools/ToolCommon/` presents a landing summary stating that canonical AgentTools binaries shared by all linked worktrees will be replaced.
- `plan validate` behaviour, exit codes (`0` ok, `2` conflict, `1` failure), and the claim record schema are unchanged.

## Verification

1. Build WorktreeCli through `/compile`.
2. Run `.agents/scripts/Test-WorktreeCliPlanScheduler.ps1`; all pre-existing cases plus the three new terminal-preparation cases pass.
3. Run `.agents/skills/finalize-changes/scripts/Test-FinalizeWorkflowFixtures.ps1` for the landing-path change.
4. Run `.agents/skills/next-plan/scripts/Test-NextPlanWorkflowSidecars.ps1`, which asserts the `prepared`/`awaiting-landing` transition at `:64` and `:230`.
5. Run `/validate-skill` for the changed `SKILL.md`, and `/update-claude-docs` for the `Tools/WorktreeCli/AGENTS.md` sentence.
6. This change itself modifies a non-Markdown path under `Tools/WorktreeCli/`, so its own landing triggers step 5b promotion and must exercise the change 5 disclosure clause.
