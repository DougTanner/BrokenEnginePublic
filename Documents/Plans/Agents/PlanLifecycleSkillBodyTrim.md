<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-31T22:14:39.003Z","dependsOn":[]} -->
# Trim Plan-Lifecycle Skill Bodies

## Context

The plan-lifecycle skills — `next-plan-review`, `next-plan`, `finalize-changes`, `external-grill-plan`, `session-audit`, `implement-plan` — carry the largest bodies outside `/compile`. Most of them declare `disable-model-invocation`, so they cost nothing until invoked; body size is their entire cost, paid in full on every invocation. Frontmatter descriptions (~19.5 KB, ~5,000 `bt-token-v1`) load every session and are not touched here. Root `AGENTS.md` routes to skills by name only, so no `AGENTS.md` edit is required.

Six regions narrate a script's enforced internals, restate root `AGENTS.md`, or state the same content twice:

1. `next-plan-review/SKILL.md` is 358 lines and the single largest per-invocation item at roughly 4,100 `bt-token-v1`. `:35-98` narrates `Find-AgentSessionTranscript.ps1` internals that the script and its own test already enforce; `:160-244` is a bespoke measurement calculus and routing tables read on every invocation.
2. `next-plan/SKILL.md:62-81` (`## Implementation approval`) restates root `AGENTS.md`'s Tier-1-continue paragraph, and `:32-39` narrates the JSON contract `Invoke-NextPlanClaim.ps1` already enforces.
3. `finalize-changes/SKILL.md` `## Bundled scripts` (`:34-73`) is a script operating manual. Inside it, `:60-66` and the matching step at `:105-108` carry a non-obvious ordering rule that is not inferable from either script's signature.
4. `external-grill-plan/SKILL.md` `## Existing-Library Gate` (`:116-153`) is a conditional gate read on every invocation. Separately, `## Plan Context` (`:155-170`) has this skill run WorktreeCli `plan validate` itself, while `.agents/skills/next-plan/references/tier3-workflow.md:27-29` assigns every repository read, search, and WorktreeCli validation the grill requires to the preparation `implementer`. Two documents assign the same job to two different roles.
5. `session-audit/SKILL.md` states the same content twice: the four conditional trigger-mode definitions in `## Required Inputs` (`:24-71`) and Method steps 3-5 (~20 lines).
6. `implement-plan` carries `references/client-compatibility.md`, 11 lines restating the first paragraph of `.agents/references/subagent-reporting.md`, which `SKILL.md:21` already cites, and a risk-audit enumeration at `:98-107`.

## Design

1. `next-plan-review`: reduce `:35-98` to the script invocation plus the result rule — `pass`, `needs-selection`, anything else is `BLOCKED`. Move `:160-244` into `.agents/skills/next-plan-review/references/measurement.md`, cited from a one-line trigger. Target: 358 lines down to about 80.
2. `next-plan`: replace `:62-81` with two sentences citing root `AGENTS.md` Change Workflow Step 1 and `/finalize-changes` — preparation that proves the work Tier 1, decision-complete, and current continues straight into implementation; the landing confirmation still applies. Trim `:32-39` to the invocation plus the statuses the caller acts on. The plain-language execution-card headings (`### What does this plan do?`, `### Why this is good for the codebase`, each 2-4 plain sentences) are kept — they exist for the user, not the agent.
3. `finalize-changes`: move `## Bundled scripts` into `.agents/skills/finalize-changes/references/scripts.md`, except for about four lines of prose kept in `SKILL.md` stating the ordering rule at `:60-66` and `:105-108`: the caller must release its reconciliation lease through `Invoke-FinalizeLockClaim.ps1 -Release` *before* landing is invoked, because landing mints its own owner token through WorktreeCli `lock token` and reads a still-held caller lease as foreign contention, failing the claim. That reason is stated exactly once, in the retained `SKILL.md` prose: the relocated `Invoke-FinalizeLanding.ps1` bullet in `references/scripts.md` cites that prose for the ordering and does not restate the foreign-contention reason. `## Landing confirmation` (`:117-147`) is byte-untouched: root `AGENTS.md` Step 8 delegates the confirmation contract to it.
4. `external-grill-plan`: move `## Existing-Library Gate` into `.agents/skills/external-grill-plan/references/library-gate.md`, cited from a one-line trigger; `## Decision Checklist` (`:90-115`) stays inline. Resolve the ownership contradiction in favour of `tier3-workflow.md`: the preparation `implementer` runs `plan validate` and supplies the inventory and the referenced Plan text in its brief, and `## Plan Context` is rewritten to consume that brief instead of running the command. The rules it keeps: consult tracked Plans only on a declared dependency, shared files or symbols, or overlap surfaced by closing checks; never read machine-local claims; never run a command that changes scheduler state; `Documents/Features` is manual and outside scheduler inventory.
5. `session-audit`: fold Method steps 3-5 into the four conditional trigger-mode definitions in `## Required Inputs` (`:24-71`), so each mode states its own procedure once.
6. `implement-plan`: delete `references/client-compatibility.md` and its link at `SKILL.md:23`, leaving the existing `.agents/references/subagent-reporting.md` citation at `:21`. Trim the risk-audit enumeration at `:98-107` to the Claim / Check / Result form. `:54-65` (search before adding a helper) and `## Phase 2: Same-Context Audit` are kept.

## Critical files

- `.agents/skills/next-plan-review/SKILL.md` — `## Prove provenance` `:35-98`, `## Measure control-work share` and `## Verify execution-model routing` `:160-244`.
- `.agents/skills/next-plan-review/references/measurement.md` — new.
- `.agents/skills/next-plan/SKILL.md` — `:32-39`, `## Implementation approval` `:62-81`.
- `.agents/skills/finalize-changes/SKILL.md` — `## Bundled scripts` `:34-73`, `## Normal workflow` step at `:105-108`; `## Landing confirmation` unchanged.
- `.agents/skills/finalize-changes/references/scripts.md` — new.
- `.agents/skills/external-grill-plan/SKILL.md` — `## Existing-Library Gate` `:116-153`, `## Plan Context` `:155-170`.
- `.agents/skills/external-grill-plan/references/library-gate.md` — new.
- `.agents/skills/next-plan/references/tier3-workflow.md` — read-only; the retained owner of the validation assignment.
- `.agents/skills/session-audit/SKILL.md` — `## Required Inputs` `:24-71`, `## Method` steps 3-5.
- `.agents/skills/implement-plan/SKILL.md` — `:23`, `:98-107`.
- `.agents/skills/implement-plan/references/client-compatibility.md` — deleted.
- `.agents/references/subagent-reporting.md`, root `AGENTS.md` — read-only authorities.

## In scope

- `next-plan-review/SKILL.md:35-98` reduced to invocation plus the `pass`/`needs-selection`/`BLOCKED` rule; `:160-244` moved to `references/measurement.md` (new) behind a one-line trigger.
- `next-plan/SKILL.md:62-81` replaced by the two citing sentences in Design item 2; `:32-39` trimmed to invocation plus caller-actionable statuses.
- `finalize-changes/SKILL.md` `## Bundled scripts` moved to `references/scripts.md` (new), with the lease-release ordering rule retained as about four lines of prose in `SKILL.md`, and the relocated `Invoke-FinalizeLanding.ps1` bullet citing that prose instead of restating the foreign-contention reason.
- `external-grill-plan/SKILL.md` `## Existing-Library Gate` moved to `references/library-gate.md` (new) behind a one-line trigger; `## Plan Context` rewritten to consume the preparation implementer's brief instead of running `plan validate`, keeping the four rules named in Design item 4.
- `session-audit/SKILL.md`: Method steps 3-5 folded into the four trigger-mode definitions in `## Required Inputs`.
- `implement-plan`: `references/client-compatibility.md` deleted, its link at `SKILL.md:23` removed, and `:98-107` rewritten in Claim / Check / Result form.

## Out of scope

- `finalize-changes/SKILL.md` `## Landing confirmation`, the confirmation contract, the primary advance, compare-and-swap, rollback, the landing lock protocol, and lease durations.
- Any change to claim lifecycle semantics, plan selection order, `plan claim-next`, `plan complete`, `plan reject`, or any scheduler state.
- Any change to the behavior of `Find-AgentSessionTranscript.ps1`, `Invoke-NextPlanClaim.ps1`, `Invoke-FinalizeLanding.ps1`, `Invoke-FinalizeLockClaim.ps1`, `FinalizeWorkflowCommon.psm1`, or any other bundled script or fixture.
- Any change to root `AGENTS.md` or to `.agents/references/subagent-reporting.md`.
- `next-plan`'s plain-language execution-card headings, `implement-plan/SKILL.md:54-65` and `## Phase 2`, and `external-grill-plan` `## Decision Checklist`.
- Every skill frontmatter, including descriptions.
- Renaming, merging, adding, or deleting any skill.

## Coordination

- `Documents/Plans/Documents/FinalizeRetainedLandingLockAdoption.md` adds a clause to the `Invoke-FinalizeLanding.ps1` bullet inside `finalize-changes` `## Bundled scripts`, the section this plan relocates. Whichever lands second applies its edit to the relocated `references/scripts.md` text rather than to `SKILL.md`. In both orders the caller-lease-is-foreign-contention reason ends up stated exactly once, in the `SKILL.md` ordering prose this plan retains, with the `references/scripts.md` bullet citing it rather than restating it.
- Commit `0695beba` ("Adopt Get-SessionChangeInventory across review skills and add Find-SessionDebugResidue scanner") already edited the changed-file-inventory bullet inside `session-audit` `## Required Inputs`, which this plan reorganizes. This plan carries that landed bullet into the reorganized trigger-mode definitions unchanged in meaning; the `BLOCKED` rules and the implementer's attribution responsibility stay unchanged.

## Risk tier and invariants

Tier 2 — scoped tool behavior across plan-lifecycle skill documentation; no engine runtime, determinism/CRC, wire, serialization, save/replay, or threading surface, and no C++ change. The landing-lock and confirmation prose that would make this Tier 3 is explicitly out of scope and unchanged, except that the lease-release ordering rule is preserved verbatim in meaning.

Invariants: the landing confirmation contract is byte-unchanged; the caller must still release its reconciliation lease before landing runs, and that reason is still stated exactly once, where the caller reads it; `plan validate` is still run exactly once per Tier-3 grill, by the preparation implementer; a missing or ambiguous required input still yields `BLOCKED`; the user-facing plain-language card headings still exist; bundled script behavior is unchanged; no skill entry point is renamed.

## Acceptance criteria

- `next-plan-review/SKILL.md` is about 80 lines, states the transcript-script invocation and the `pass`/`needs-selection`/`BLOCKED` rule, and its measurement calculus and routing tables live in `references/measurement.md`.
- `next-plan/SKILL.md` no longer restates the Tier-1-continue paragraph and cites root `AGENTS.md` Step 1 and `/finalize-changes` instead; the two plain-language card headings are unchanged.
- `finalize-changes/SKILL.md` contains no script operating manual, still states the lease-release-before-landing rule and its reason in about four lines, and `## Landing confirmation` is byte-identical to today. The foreign-contention reason appears exactly once across the package — in `SKILL.md` — and the `Invoke-FinalizeLanding.ps1` bullet in `references/scripts.md` cites it.
- `external-grill-plan/SKILL.md` contains no `plan validate` command line, consumes the preparation implementer's inventory, and still forbids reading machine-local claims and running any scheduler-changing command; `references/library-gate.md` holds the gate and `## Decision Checklist` is unchanged.
- `session-audit/SKILL.md` states each trigger mode's procedure once, with no duplicate Method steps 3-5, and its `BLOCKED` rules are unchanged.
- `implement-plan/references/client-compatibility.md` no longer exists, no link points at it, and the `.agents/references/subagent-reporting.md` citation remains; the risk audit is in Claim / Check / Result form.
- Each edited `SKILL.md` measures smaller than today by `.agents/scripts/Measure-Tokens.ps1`.
- `/validate-skill` passes on every touched `SKILL.md`, and every added reference link resolves.
- No bundled script is edited and no skill entry point is renamed.
