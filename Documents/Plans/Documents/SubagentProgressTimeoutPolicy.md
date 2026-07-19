# Preserve Progress During Long-Running Subagent Reviews

## Context

The 2026-07-19 `/next-plan` preparation for `Documents/Plans/Frame/MissileLifetimeAndTargetLifecycle.md` restarted the delegated plan audit three times because elapsed time and manager wait boundaries were treated as evidence that each reviewer was stuck. Transcript inspection shows the opposite:

| Reviewer | Observed turn duration | Distinct tool calls | Identical tool-input groups | Last tool activity before interruption |
|---|---:|---:|---:|---:|
| Raman (`missile_plan_audit`) | 317.8 s | 28 | 0 | 9.2 s |
| Darwin (`missile_plan_audit_fast`) | 262.1 s | 20 | 0 | 3.4 s |
| Nietzsche (`missile_plan_audit_retry`) | 213.6 s | 17 | 0 | 49.9 s |

Each reviewer was still making distinct, plan-grounded repository reads near the interruption. None repeated an equivalent tool-call cycle, revisited the same unchanged failure, or otherwise showed transcript evidence of a loop. The first reviewer should have been allowed to finish; if interruption was required, the manager should have requested and preserved findings gathered so far before deciding whether any continuation was necessary. Restarting from scratch discarded useful exploration and repeated its cost.

The governing instructions already require an interrupted or re-scoped reviewer to return findings gathered so far (`AGENTS.md`, **Subagents**) and require `/plan-audit` to return partial findings immediately when constrained or interrupted (`.agents/skills/plan-audit/SKILL.md`, **Reporting Mode**). They do not give the manager an observable liveness test, distinguish a wait timeout from a stalled reviewer, or require reuse of partial work before spawning a replacement. That missing manager-side contract is the acceptance gap.

Evidence transcripts:

- Main session: `C:\Users\dougt\.codex\sessions\2026\07\19\rollout-2026-07-19T11-48-53-019f7b10-e8d8-7830-8f7d-7e1952b0bc3c.jsonl`
- Raman: `C:\Users\dougt\.codex\sessions\2026\07\19\rollout-2026-07-19T11-52-29-019f7b14-33b7-7bc1-a9ba-11a0fb162fa4.jsonl`
- Darwin: `C:\Users\dougt\.codex\sessions\2026\07\19\rollout-2026-07-19T11-59-25-019f7b1a-8cb6-7a61-a616-5c3e5ffc2d81.jsonl`
- Nietzsche: `C:\Users\dougt\.codex\sessions\2026\07\19\rollout-2026-07-19T12-03-54-019f7b1e-a6c4-76b3-b672-838ae221c91f.jsonl`

## Design

1. Add one manager-side liveness rule to the shared delegation contract: a wait boundary or elapsed-time threshold alone is not evidence that a subagent is stuck. Recent distinct tool activity, narrowing searches, new evidence, or an in-progress synthesis indicates forward progress. A loop requires repeated equivalent operations or unchanged failures without narrowing or new evidence.
2. Define the interruption sequence for delegated reviews. Before replacing a running reviewer, inspect available status/transcript evidence, request an immediate return of findings gathered so far, and allow a bounded response window. Interrupt or replace only after a terminal failure or documented no-progress/loop evidence.
3. Preserve accumulated work across continuation. Prefer resuming the same reviewer when the host supports it; otherwise give the replacement the prior findings and decisive transcript evidence so it continues from the interruption point instead of restarting repository exploration.
4. Apply the rule at the `/next-plan` delegated `/plan-audit` call site and keep `/plan-audit`'s existing partial-return obligation aligned with the manager contract. Keep the policy small and observable; do not introduce a general scheduler, fixed wall-clock completion promise, or model-specific timeout table.
5. Validate the resulting workflow instructions against the four evidence transcripts. The recorded activity above must classify all three reviewers as progressing rather than looping, and the prescribed action must be to wait or request partial findings rather than spawn a fresh reviewer from scratch.

## Critical files

- `AGENTS.md` — shared **Subagents** manager contract.
- `.agents/references/subagent-reporting.md` — concise partial-work handoff contract.
- `.agents/skills/next-plan/SKILL.md` — delegated `/plan-audit` orchestration and replacement decision.
- `.agents/skills/plan-audit/SKILL.md` — existing interrupted-review partial-return behavior; change only if alignment requires it.
- `C:\Users\dougt\.codex\sessions\2026\07\19\rollout-2026-07-19T11-48-53-019f7b10-e8d8-7830-8f7d-7e1952b0bc3c.jsonl` — main-session chronology and manager decisions.
- `C:\Users\dougt\.codex\sessions\2026\07\19\rollout-2026-07-19T11-52-29-019f7b14-33b7-7bc1-a9ba-11a0fb162fa4.jsonl` — first reviewer activity.
- `C:\Users\dougt\.codex\sessions\2026\07\19\rollout-2026-07-19T11-59-25-019f7b1a-8cb6-7a61-a616-5c3e5ffc2d81.jsonl` — second reviewer activity.
- `C:\Users\dougt\.codex\sessions\2026\07\19\rollout-2026-07-19T12-03-54-019f7b1e-a6c4-76b3-b672-838ae221c91f.jsonl` — third reviewer activity.

## Out of scope

- Re-running or completing the interrupted missile plan audit.
- Changing Codex service limits, model behavior, collaboration APIs, or host scheduling.
- Adding a general-purpose transcript analytics system, background watchdog, or configurable timeout framework.
- Changing engine/runtime code, build configuration, or the missile lifecycle plan itself.

## Acceptance criteria

- Shared instructions state that a wait timeout or elapsed duration alone never proves a loop, and give observable forward-progress and no-progress signals.
- Delegated review orchestration requires partial-work recovery before replacement and prevents a fresh replacement while the original reviewer is making recent distinct progress.
- A replacement, when justified, receives the original reviewer's findings or extracted evidence and does not repeat completed exploration from scratch.
- A transcript-based walkthrough classifies Raman, Darwin, and Nietzsche as progressing, with no loop proven, and concludes that allowing the first reviewer to finish was the correct action.
- `/validate-skill` passes for every changed skill, documentation links resolve, and `git diff --check` passes. No client/server build or agent-harness run is required.

## Notes

This is workflow/documentation hardening. Implementation is expected to remain Tier 1 if it changes instructions only; adding executable transcript analysis would make it Tier 2 tool behavior and require a plan audit plus a focused observable scenario. Queue mutation and landing still use the final-evidence path. There is no determinism/CRC, `kiVersion`/`.pack`, replay, wire protocol, client/server guard, allocation-tracked runtime, shader, build, or live game exposure.
