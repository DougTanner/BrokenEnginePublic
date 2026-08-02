<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-02T01:12:03.039Z","dependsOn":[]} -->
# Next Plan Selection Contract Wording

## Context

Three `/next-plan` contract sentences produced proven wasted work in the audited landings:

1. Targeted-claim `none-available`. `.agents/skills/next-plan/SKILL.md:42` calls `none-available` "a normal stop with nothing to claim". For a `-Plan`-targeted invocation, Codex session `019fbeb5-453b` (landing `c7a85890`) read that as a whole-turn hard stop — "The skill explicitly requires stopping on `none-available` without retrying or reordering" (19:08:01Z) — ending the turn with nothing claimed and forcing a 13.4-minute user correction, when the status only meant that one plan was ineligible.
2. Deferral override. `SKILL.md:95` — "never defer after final preparation has run" — has no explicit-user-override path. In Codex session `019fbe73-7e74` (landing `e93f52fa`) the parent first told the user deferral was forbidden (17:58:02Z), then correctly ran `Defer-NextPlan.ps1` after the user's override (18:48:16Z); the authority order makes the override valid, but the agent had to improvise against the written contract.
3. Bundled-script isolation. `Invoke-NextPlanClaim.ps1` combined with `git status`/`git rev-parse` in one shell call returned the claim JSON concatenated with a commit hash, unparseable, and the mutation-capable script was re-run standalone (`code:"reused"`, session `019fbe73`, 18:48:39-18:48:48Z).

Root cause in each case is a contract sentence, not agent judgment: the wording either overstates a stop, omits a documented exception, or leaves an invocation hazard unstated.

## Design

Three wording edits in `.agents/skills/next-plan/SKILL.md`, each stated once:

1. Split the `none-available` sentence: for a bare selection it remains a normal whole-skill stop; for a `-Plan`-targeted invocation it means that plan is ineligible, and the manager may select or claim a different candidate within the same turn. Retry-storm protection stays with the bare-selection stop and the existing no-repair rule for other statuses.
2. Add the explicit-user-override exception to the deferral rule: after final preparation, deferral requires an explicit user instruction in the current session, which is recorded in the handoff; nothing else unlocks it.
3. Add one sentence to the claim-script paragraph: run the bundled script as its own shell call, never combined with other commands, so its single JSON object stays parseable and the mutation-capable script is never re-run to disambiguate output.

## Critical files

- `.agents/skills/next-plan/SKILL.md` — the three sentences above.

## In scope

- `next-plan/SKILL.md`: the targeted-claim `none-available` clause, the deferral-override clause, and the one-script-per-call sentence.

## Out of scope

- `Invoke-NextPlanClaim.ps1`, `Defer-NextPlan.ps1`, and all other bundled scripts.
- Selection mechanics and the scheduler surface (owned by `Documents/Plans/Agents/PlanSchedulerEligibleListing.md`).
- The claim lifecycle, lease durations, and healing rules.

## Risk tier and invariants

Tier 1 — mechanical documentation corrections in one skill body with no behavior, script, or invariant exposure.

Invariants: bare-selection stop semantics unchanged; the deferral gate stays closed to agent judgment; each rule stated exactly once.

## Acceptance criteria

- `/validate-skill` passes on the edited `SKILL.md`.
- The three clauses read exactly one way: a targeted `none-available` permits same-turn reselection; post-preparation deferral requires an explicit in-session user instruction; the claim script runs as its own shell call.

## Coordination

`Documents/Plans/Agents/PlanSchedulerEligibleListing.md` rewrites the selection step of the same `SKILL.md` to use the new `plan list` verb. Whichever lands second merges its wording with the other's edits rather than restating them.
