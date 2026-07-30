<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-29T18:00:59.824Z","dependsOn":["Documents/Plans/Documents/DeterministicVcxprojValidation.md"]} -->
# Give decision-complete Tier-1 Plans a bounded execution path

## Context

The current `/next-plan` path applies preparation, a user implementation-approval gate, implementation, fresh coherence review, triggered hygiene, a separate acceptance reviewer, terminal preparation, `/verify-changes`, finalization, optional review-tool launch, and landing confirmation even to a three-line mechanical Plan.

Commit `4f74a207` followed that route correctly but took 52m17s wall time, 31m52s excluding user pauses, eight child executions, and 10.56M exposed tokens. The duplicated reviewer/final-evidence stages and terminal-preparation child repeated unchanged inputs. Executable Plans now contain an explicit scope ceiling, and changed-region review independently checks authorization.

## Design

An explicit `$next-plan` invocation is implementation authority when repository-backed preparation confirms all of the following: the actual change is Tier 1, the executable Plan is decision-complete, current code does not require a material scope or acceptance change, and no user or architectural decision remains. Present a compact execution notice and continue without an implementation-approval pause.

Any tier escalation, Plan/repository contradiction, material scope or acceptance delta, obsolete problem requiring disposition, or unresolved decision falls back to the existing complete presentation and approval gate. Tier 2 and Tier 3 otherwise keep their existing plan-review and approval requirements.

The normal Tier-1 route is:

1. One retained preparation/implementation worker claims and validates the Plan, confirms current-tree scope, records only the Plan path, tier trigger, owned-path ceiling, acceptance commands, and roles, then makes the smallest change.
2. The same worker runs deterministic checks and triggered Tier-1 hygiene. For project metadata, it invokes `Test-VcxprojPair.ps1` directly rather than dispatching a mechanic merely to run the command.
3. The same worker performs receipt-internal terminal preparation and hands the owned paths to candidate preparation. There is no standalone terminal-preparation child.
4. Finalization prepares the immutable Git candidate from the prerequisite Plan.
5. One fresh read-only reviewer examines that exact candidate and combines changed-region authorization, artifact coherence/correctness, and every Tier-1 acceptance criterion in one result.
6. An accepted finding returns to the retained worker, creates a replacement candidate, and reruns the combined review. Otherwise the retained finalizer prepares the landing summary.
7. Main obtains one explicit landing confirmation, then resumes the finalizer for primary mutation.

Do not invoke `/plan-audit`, a separate coherence or acceptance reviewer, `/verify-changes`, or `/session-audit` on the normal Tier-1 path. A conflict, changed reconciliation delta, or explicit audit request may still trigger the applicable higher-signal path.

SmartGit is opt-in for every tier. Approval preparation returns a concise manual review command without launching it. Invoke the existing review-window sidecar only after an explicit user request.

## Critical files

- Root `AGENTS.md` Change Workflow and `.agents/skills/next-plan/SKILL.md` — Tier-1 authority and routing.
- Next-plan execution gates and `.agents/skills/finalize-changes/SKILL.md` — candidate review, confirmation, and opt-in SmartGit.
- `.agents/references/subagent-reporting.md` — compact Tier-1 handoff and combined-review result.

## Out of scope

- Automatic authority for Tier 2 or Tier 3, or bypassing a material Plan delta.
- Removing the explicit landing confirmation.
- Skipping a deterministic validator, compilation, or runtime check genuinely triggered by changed bytes.
- Changing reviewer model selection or weakening Tier-2/Tier-3 review.

## Risk tier and invariants

**Tier 3.** Trigger: changes approval and final-evidence integration for claimed executable Plans.

Only proven Tier-1 work may take the fast path; the Plan remains the target and ceiling; every changed region remains independently authorized; one fresh reviewer remains independent of implementation; changed candidates are re-reviewed; primary remains untouched before confirmation; terminal claim release still occurs only after landed-state proof. No engine runtime invariant is exposed.

## Acceptance criteria

- A decision-complete Tier-1 next-plan run uses at most three child executions before completion: one retained preparation/implementation worker, one combined reviewer, and one retained finalizer.
- Exactly one reviewer runs unless an accepted finding or changed reconciliation delta creates a replacement candidate.
- The normal path has no implementation-approval pause, plan audit, separate acceptance review, `/verify-changes`, session audit, automatic SmartGit launch, or standalone terminal-preparation worker.
- Mapping the `4f74a207` Plan through the new contract yields one project validator invocation and one candidate reviewer while still authorizing the three GUID regions and mechanical Plan lifecycle changes.
- A Tier escalation, material Plan delta, or unresolved decision demonstrably returns to the full approval path.
- Landing still requires the self-contained summary and explicit confirmation.
- Changed skills pass `/validate-skill`; next-plan and finalization integration fixtures pass.

## Notes

The prerequisite Plans supply the two properties this route relies on: deterministic project verification and immutable candidate-bound final evidence.
