# Canonicalize finalization preflight

## Context

Worktree isolation succeeded in all three audited sessions: changes stayed in their assigned worktrees, reconciliation preserved content, and no data corruption was found. Finalization nevertheless stopped falsely when ad-hoc checks compared slash versus backslash paths, parsed a literal `\t` instead of the manifest's tab separator, or guessed the wrong AgentCli executable path.

The retained session claims are not stale: `Get-AgentCliExclusionStatus` currently reports the three wrapper PowerShell hosts as live, and `Start-AgentWorktreeSession.ps1` intentionally unregisters each claim only when its tracked client exits and wrapper teardown runs. The gap is deterministic preflight and status wording, not a replacement worktree/claim lifecycle.

## Design

1. Add one finalization preflight sidecar that consumes the five wrapper provenance values, canonicalizes the worktree/primary/Git-common paths with the repository's existing Windows path helpers, and emits structured JSON. All identity comparisons use one separator/case policy and reject ambiguous/root-escaping paths.
2. Parse changed-file manifests as NUL-delimited Git paths and actual tab-separated ledger records. Do not split human-formatted lines, interpret literal escape sequences, or compare unnormalized path strings.
3. Resolve AgentCli only from the adopted session worktree's provisioned `Tools/AgentCli/Platforms/VisualStudio2026/Output/AgentCli.exe`, then validate its resolved link/target and required command capability. Never probe a removed global install path or guess from the primary current directory.
4. Have `/finalize-changes` invoke the sidecar once before mutation and consume its structured fields for primary/worktree identity, executable, manifest comparison, and live wrapper claim. Remove duplicated prose-command recipes that let agents reconstruct these checks differently.
5. Classify wrapper session status explicitly: `expected-live` when owner, PID/start time, repository, and worktree match the active wrapper provenance; `stale` only when the existing module's liveness check proves the host dead; `mismatch` for conflicting live identity. An expected-live claim is required through landing and is released by wrapper teardown, not treated as cleanup debt.
6. Preserve landing-lock leases, recovery, all-worktree Git-marker checks, row-claim release, and mandatory user landing approval unchanged.
7. Verify with disposable repositories/worktrees covering mixed separators/case, spaces and tabs in filenames, deleted files, symlinked Output, wrong/missing executable, mismatched provenance, live wrapper claim, dead-host cleanup, and an active Git-operation marker. Use existing wrapper/claim modules; do not invent another registry.

## Critical files

- `.agents/skills/finalize-changes/SKILL.md` — single sidecar invocation and structured-result contract.
- New `.agents/skills/finalize-changes/scripts/Test-FinalizePreflight.ps1` (or equivalent) — canonical identity, manifest, executable, and claim checks.
- `.agents/scripts/AgentCliSessionExclusion.psm1` — read-only status classification only if the sidecar cannot derive it without changing the module.
- `.agents/scripts/Start-AgentWorktreeSession.ps1` and `AGENTS.md` — clarify expected-live claim lifetime and wrapper-owned teardown; no lifecycle redesign.

## Out of scope

- Replacing worktree isolation, changing wrapper admission, or releasing a live wrapper claim at landing.
- Changing AgentCli landing-lock schema/lease/recovery; those were handled by the landed `LandingLockLifecycleHardening` work.
- Reintroducing global AgentCli install paths or changing build/bootstrap policy.
- Weakening manifest equality, Git-operation, cleanliness, ancestry, or user-approval gates.
- Adding unit tests.

## Acceptance criteria

- Mixed slash/backslash/case representations of the same canonical Windows path compare equal, while distinct or escaping paths fail with one exact diagnostic.
- Filenames containing spaces or tabs and deletion records round-trip through the manifest comparison without literal-escape parsing.
- Finalization obtains AgentCli from the adopted worktree Output path, validates it, and reports a precise missing/wrong-target blocker without probing alternate installs.
- A matching live wrapper claim reports `expected-live` and permits finalization; dead or mismatched claims are distinguished and handled by the existing owner/liveness rules.
- The three retained audited worktrees pass read-only preflight without the prior false blockers.
- Landing approval, lease refresh/release, row unclaim, worktree containment, and final manifest equality remain unchanged.
- Changed skills pass `/validate-skill`; PowerShell parser and disposable preflight fixtures pass under the supported host.

## Notes

- Developer workflow only; no engine runtime, determinism/CRC, wire, save/replay, `.pack`, shader, or guard-affinity exposure.
- Score: Effort 2, Impact 3, Risks 2, total 1; Tier Small.
- Historical lock-lifecycle plans are satisfied and are not duplicated here.

