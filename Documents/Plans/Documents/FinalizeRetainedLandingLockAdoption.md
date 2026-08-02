<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-30T23:12:24.106Z","dependsOn":[]} -->
# Finalize Retained Landing Lock Adoption

## Context

A failed landing can leave the global landing lock held by a dead owner, and the next landing attempt from the same session contends against that abandoned claim for the full lease.

`.agents/skills/finalize-changes/scripts/Invoke-FinalizeLanding.ps1`:

- lines 313-319 — every invocation mints a brand-new owner through WorktreeCli `lock token` and claims the landing lock with `-LeaseSeconds 3600`. There is no path that reuses or adopts an existing owner.
- lines 356-357 — the `finally` block always calls `Release-LandingLockIfSafe`.
- lines 171-179 — that function returns without releasing when `Test-FinalizeAllWorktreesClear` reports any registered worktree as uninspectable or carrying a Git operation marker; it only records `Landing lock retained: <problem>` residuals. This retention is the deliberate rule stated in `.agents/skills/finalize-changes/SKILL.md` ("On failure, release an owned landing claim only after all registered worktrees are inspectable and free of Git operation markers; otherwise retain and report it.").

The retained claim is live for up to 3600 seconds under an owner that no longer exists, because the minting process has exited. A re-invocation mints a different owner, so `Invoke-FinalizeLandingLockClaim` in `.agents/scripts/FinalizeWorkflowCommon.psm1` sees a live foreign lease: its single blocking WorktreeCli `lock claim` waits out its bound and returns contention, and its already-owned adoption test still compares owner, session, and worktree, where the owner never matches. The helper therefore returns `landing-lock.retryable-wait`, which the landing script reports as `landing-lock.claim-failed`. The session blocks itself, and every other session's landing blocks too, until the lease expires.

This is the same defect class as `Documents/Plans/Documents/FinalizeLandingLeaseRelease.md` — self-contention against a lock the session itself holds — but a different actor: there the caller's reconciliation lease, here the landing script's own retained claim from a prior invocation. That plan explicitly fenced the failure-path retention rule and any change to `Invoke-FinalizeLanding.ps1` out of scope, so the retained-claim case was never addressed.

Not affected: the documented "process died after primary advanced" recovery. `Invoke-FinalizeLanding.ps1` lines 300-308 take the already-ancestor fast path and exit before any lock claim.

## Design

Let a landing invocation adopt a live landing lock that its own session and worktree previously claimed, instead of treating it as foreign contention. Two script edits in `Invoke-FinalizeLanding.ps1` plus the `SKILL.md` clause described at the end of this section:

1. Claim under a landing-scoped session identity rather than the raw `$SessionLabel` — a single derived value such as `"$SessionLabel/landing"` used for both the claim and the adoption test. This keeps landing claims distinguishable from the caller's reconciliation lease, which claims under the plain `$SessionLabel` through `scripts/Invoke-FinalizeLockClaim.ps1`. The caller's still-held reconciliation lease therefore remains foreign contention exactly as `SKILL.md` now states; only a prior landing's own claim becomes adoptable.
2. Before minting a fresh token at lines 313-317, read the lock through `Get-FinalizeLandingLockState`. When it reports `Kind` `live`, its `session` equals the landing-scoped identity, and its `worktree` resolves to the same Windows identity as `$script:CurrentIdentity.Worktree`, set `$script:LandingOwner` to the reported `owner` instead of minting a new one. Mint a fresh token in every other case.

No further change is needed: with an adopted owner, `Invoke-FinalizeLandingLockClaim` reaches its existing already-live-and-owned branch and returns claimed, `Refresh-LandingOwner` extends the lease under that owner, and cleanup releases it normally once the worktrees are inspectable. An expired retained claim is recovered inside WorktreeCli's blocking claim by its guarded expired-lease takeover. WorktreeCli's `--session` argument is an arbitrary nonempty string (`Tools/WorktreeCli/LandingLockCommands.cpp` lines 254-256), so no tool change is required.

Safety of the adoption: a session owns exactly one worktree and lands from it serially, so the `(landing session identity, worktree)` pair identifies one landing actor. Adoption never touches a lock held by another session, another worktree, or the caller's reconciliation lease, and it never overrides `leaseState` `unverifiable`, which still requires user authority.

Reject the alternatives of shortening the 3600-second lease (it must outlive a real landing) and of releasing the claim unconditionally on failure (that discards the deliberate retention rule protecting an uninspectable repository).

Also add one clause to the `Invoke-FinalizeLanding.ps1` script bullet in `.agents/skills/finalize-changes/SKILL.md` stating only the adoption rule: a re-invocation adopts a landing claim its own session and worktree retained. For the foreign-contention side, that clause cites the existing `SKILL.md` lease-release ordering prose rather than restating it, so the caller-lease-is-foreign-contention reason stays stated exactly once. Leave the release/retention rule itself unchanged.

## Critical files

- `.agents/skills/finalize-changes/scripts/Invoke-FinalizeLanding.ps1` — owner mint and claim at lines 313-319; retention path via `Release-LandingLockIfSafe` at lines 171-179 and the `finally` block at lines 356-357.
- `.agents/scripts/FinalizeWorkflowCommon.psm1` — `Get-FinalizeLandingLockState` (lines 302-327), `Test-FinalizeLandingLockClaimIdentity` (lines 291-300), and the claim policy including the already-owned branch are read and reused unchanged; `Get-FinalizeLandingLockState` is already exported, so no module edit is needed.
- `.agents/skills/finalize-changes/SKILL.md` — the `Invoke-FinalizeLanding.ps1` script bullet and the release/retention rule that follows it.
- `.agents/skills/finalize-changes/scripts/Test-FinalizeWorkflowFixtures.ps1` — the landing fixture sequence (around lines 415-470) that must still pass, extended only if the adoption path needs coverage there.

## Out of scope

- Any change to WorktreeCli lock commands, lock metadata, or the lock file format.
- Changing lease durations, wait windows, poll intervals, retry outcomes, or the `unverifiable` user-authority rule.
- Changing the failure-path retention rule itself, or when `Release-LandingLockIfSafe` may release.
- The caller-side reconciliation lease contract owned by `Documents/Plans/Documents/FinalizeLandingLeaseRelease.md`.
- The candidate parent projection owned by `Documents/Plans/Documents/FinalizeCandidateParentProjection.md`.
- Any change to the landing confirmation contract, the primary advance, or rollback.

## Risk tier and invariants

Tier 3 — the landing lock is build/landing coordination that can block other sessions, the root `AGENTS.md` Tier-3 trigger; the change also alters the exclusive-owner contract of that lock.

Invariants to preserve: exactly one owner holds the landing lock during a primary advance; a lease belonging to a different session or worktree is never adopted or stolen; `leaseState` `unverifiable` still requires user authority; no lease is held across an open-ended user wait; the failure-path retention rule, the compare-and-swap advance with rollback, and the exactly-one-confirmation landing contract are unchanged.

## Acceptance criteria

- A landing invocation that finds a live landing lock recorded under its own landing-scoped session identity and worktree proceeds under that existing owner instead of failing with `landing-lock.claim-failed`.
- A live lock recorded under any other session or worktree, and a caller-held reconciliation lease under the plain session label, still fail the claim as foreign contention.
- `Test-FinalizeWorkflowFixtures.ps1` and `Test-LandingLockStatusFixtures.ps1` pass.
- `/validate-skill` passes on the edited `SKILL.md`.

## Notes

The design assumes the `SKILL.md` script text added by `Documents/Plans/Documents/FinalizeLandingLeaseRelease.md` — landing claims under its own fresh owner token, so the caller must release its reconciliation lease first. If that text is absent when this plan is implemented, state the lease-release ordering rule and its foreign-contention reason once in `SKILL.md` and have the adoption clause cite it, rather than restating that reason inside the adoption clause.

## Coordination

`Documents/Plans/Agents/PlanLifecycleSkillBodyTrim.md` moves `.agents/skills/finalize-changes/SKILL.md` `## Bundled scripts` into `references/scripts.md`, keeping the lease-release ordering rule in `SKILL.md` as prose. That is the section holding the `Invoke-FinalizeLanding.ps1` bullet this plan amends. Whichever lands second applies its edit to the relocated reference text rather than to `SKILL.md`; in both orders the statement that a caller-held reconciliation lease is foreign contention, and that a prior landing's own retained claim is adoptable, ends up stated exactly once.

`Documents/Plans/Documents/FinalizeLandingLeaseContinuity.md` landed and replaced the release-before-land ordering this plan's caller-lease-is-foreign premise relies on: the caller's reconciliation lease is now a continuable same-actor lease instead of foreign contention. This plan therefore lands second and reconciles the ownership-identity test and restates the `SKILL.md` ownership rules exactly once — a continued caller lease and an adopted retained claim are both same-actor continuations; everything else stays foreign.
