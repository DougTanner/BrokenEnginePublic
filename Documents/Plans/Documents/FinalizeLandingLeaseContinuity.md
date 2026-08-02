<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-02T01:11:49.549Z","dependsOn":[]} -->
# Landing Lease Continuity And Internal Retry

## Context

`.agents/skills/finalize-changes/SKILL.md:45-47` requires the caller to release its reconciliation lease before invoking `scripts/Invoke-FinalizeLanding.ps1`, because the landing script mints its own owner token and treats a still-held caller lease as foreign contention. The released window between `-Release` and the landing claim is a race other sessions win, and the landing script performs exactly one advance attempt, so all wait/retry/rebase orchestration lives in the calling agent.

Measured cost across two audited landings (next-plan-review over the last six primary commits):

- Codex session `019fbea8-abbe-7c21-8316-0242817713ef` (landed `3ebf662f`): the finalizer lost the release-to-advance window twice (23:58:27Z claim → 23:58:35Z landing refused; 00:00:55Z → 00:01:03Z refused), totalling 11 lock-claim calls, 5 landing invocations, and 7 rebases; finalizer active time 2,540 s and ~12.3 M tokens, dominated by this loop.
- Codex session `019fbe73-7e74-7142-acbf-634a264671c2` (landed `e93f52fa`): the finalizer agent hand-rolled `Start-Sleep 45-50` polling plus manual rebases across three stale-primary cycles (~1,400 s, ~13.9 M tokens), returned `BLOCKED`, and needed a manual user "Retry" 33 minutes later to land byte-identical content.

Root cause: lease ownership is discontinuous across the reconcile→land boundary, and the landing script has no bounded internal wait or clean-identical-rebase retry, so the agent re-implements both around it every landing.

## Design

1. Lease continuity: give `Invoke-FinalizeLanding.ps1` a parameter accepting the caller's reconciliation-lease owner token. When supplied and the live lock's session and worktree match the current identity, the script continues under that owner instead of minting a new one; the caller no longer releases before landing. Remove the release-before-land ordering rule from `SKILL.md` and from `scripts/Invoke-FinalizeLockClaim.ps1` guidance; without the parameter the current mint-fresh path is unchanged.
2. Internal bounded retry: when the advance fails with `sanity.git.primary-tip-changed`, the script itself re-runs the rebase, proves patch identity (zero-context patch hash equal to the confirmed patch), and re-attempts the compare-and-swap, up to a small fixed attempt count and deadline. A non-identical patch aborts with a distinct blocked code so the manager re-runs review of affected regions per Change Workflow Step 8; the script never lands changed bytes.
3. Foreign contention: when another session holds the lock, the script performs one bounded lease-aware wait internally (poll interval and deadline as script constants) instead of returning immediately for the agent to sleep-poll. Deadline exhaustion returns the existing retryable outcome.

The compare-and-swap advance, rollback on failure, exactly-one-confirmation contract, and the rule that a clean identical rebase does not re-ask confirmation are unchanged.

## Critical files

- `.agents/skills/finalize-changes/scripts/Invoke-FinalizeLanding.ps1` — owner mint/claim and the single-shot advance path.
- `.agents/scripts/FinalizeWorkflowCommon.psm1` — claim/adoption policy and lock-state readers reused by the continuity check.
- `.agents/skills/finalize-changes/scripts/Invoke-FinalizeLockClaim.ps1` — the `-Release` step whose pre-landing use is removed.
- `.agents/skills/finalize-changes/SKILL.md` — the lease-release ordering prose at lines 45-47 and the landing step description.
- `.agents/skills/finalize-changes/scripts/Test-FinalizeWorkflowFixtures.ps1` and `Test-LandingLockStatusFixtures.ps1` — fixture coverage for the new paths.

## In scope

- `Invoke-FinalizeLanding.ps1`: the new lease-continuity parameter, the identity check before minting, the bounded stale-primary rebase-and-retry loop with patch-identity proof, and the bounded foreign-lease wait.
- `FinalizeWorkflowCommon.psm1`: only the claim-policy branch needed to recognize the continued owner.
- `Invoke-FinalizeLockClaim.ps1` and `SKILL.md`: removal of the release-before-land ordering and its stated reason, replaced by the continuity contract stated once.
- Fixture additions covering continuity, identical-rebase retry, non-identical abort, and foreign-lease wait.

## Out of scope

- WorktreeCli lock commands, lock metadata, or the lock file format.
- The retained-claim adoption owned by `Documents/Plans/Documents/FinalizeRetainedLandingLockAdoption.md`, and the failure-path retention rule.
- The landing confirmation contract, rollback, `/verify-changes`, and any change to what constitutes a meaningful diff change.
- Adopting or waiting out a lease held by a different session or worktree beyond the existing bounded-wait outcome.

## Risk tier and invariants

Tier 3 — the landing lock is build/landing coordination that can block other sessions, and the change alters the lock ownership contract across the reconcile→land boundary.

Invariants: exactly one owner holds the landing lock during a primary advance; a lease belonging to another session or worktree is never adopted or stolen; the compare-and-swap advance with rollback is unchanged; no lease is held across an open-ended user wait — the confirmation pause still happens with no landing claim held; an internal retry lands only a byte-identical patch.

## Acceptance criteria

- A landing invoked with the caller's reconciliation-lease token proceeds without `landing-lock.claim-failed` and without a release-claim gap another session can win.
- With primary advancing between confirmation and advance, a byte-identical patch lands within one script invocation, with no agent-side rebase or sleep loop.
- A rebase producing a non-identical patch returns a distinct blocked code and does not advance primary.
- `Test-FinalizeWorkflowFixtures.ps1` and `Test-LandingLockStatusFixtures.ps1` pass; `/validate-skill` passes on the edited `SKILL.md`.

## Coordination

`Documents/Plans/Documents/FinalizeRetainedLandingLockAdoption.md` lets a landing adopt its own prior retained landing claim and relies on the caller's reconciliation lease remaining foreign contention under the current release-before-land ordering — the premise this plan replaces with lease continuity. Whichever lands second reconciles the ownership-identity test and restates the `SKILL.md` ownership rules exactly once: a continued caller lease and an adopted retained claim are both same-actor continuations, and everything else stays foreign.
