# Blind-Spot and Interview Guidance

Status: exploratory / investigation. It lives in `Documents/Investigations/` because it presents options rather than a decision-complete implementation, so it is never a scheduler input. It becomes a Plan only once the open questions below are answered and it moves to `Documents/Plans/<area>/` with byte-zero `broken-engine-plan/v1` metadata.

## Context

Current Anthropic guidance for the Claude 5 family recommends two pre-implementation practices this repository does not name anywhere:

- A blind-spot pass — asking the model to surface the user's unknown unknowns before work begins, given explicit context about what the user does and does not already know.
- A one-question-at-a-time interview — prioritizing questions whose answers would change the architecture, rather than presenting a batch of options.

The repository already performs a version of the second practice, but only at Tier 3 and only for plans.

## Open questions

1. Does this duplicate `/external-grill-plan`? That skill already interviews the user about decisions requiring judgment, and root `AGENTS.md` step 2 routes every Tier-3 change through it after `/plan-audit`. The candidate gap is Tier 2 and pre-plan exploration, where no interview exists — but adding one risks unnecessary extra work for the tier the workflow deliberately keeps light.
2. Where would a blind-spot pass attach? It is pre-plan by nature, which is before step 1 pins the process baseline. The workflow currently has no phase for it.
3. Is it a skill, a directive, or nothing? It may be judgment a capable model already applies when the user's request signals unfamiliarity, in which case documenting it adds tokens for no behavior change — the exact failure mode the trim this document accompanies was meant to correct.

## Known conflicts

- `/external-grill-plan` owns the Tier-3 interview and explicitly declines Tier-1 and Tier-2 work. Any new interview guidance must not create a second, competing route.
- Root `AGENTS.md` `## Resolving Ambiguity` already defines when to ask the user versus decide: trivial choices are self-resolved, non-trivial ties go to `researcher` fan-out, architectural decisions stop and ask. A blind-spot pass would need to fit that ladder rather than bypass it.
- The `## Directives` minimum-sufficient-change rule treats speculative process as cost, not value.

## Possible approach

Cheapest viable option first: add nothing, and test whether the practice already emerges. If it does not, prefer a short `## Resolving Ambiguity` clause over a new skill — the ladder is the natural owner, and a clause costs ~30 tokens against a skill's ~60 lines.

## Out of scope

Changing `/external-grill-plan`'s tier gating, or adding any interview step to Tier 1.
