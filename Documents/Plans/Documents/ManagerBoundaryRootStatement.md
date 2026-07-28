<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-26T22:48:43.781Z","dependsOn":[]} -->
# Manager Boundary Root Statement

Status: open decision / record. The Design question below is unanswered, so a session claiming this Plan resolves that question with the user before any implementation.

## Context

The Plan completed by `c369fd01` carried a Design item requiring an explicit prohibition: "Prohibit the main from inspecting task repository code, diffs, logs, or transcripts; editing tracked artifacts; building, testing, or reviewing; and performing Plan-claim, Git, reconciliation, or landing mechanics."

That session's first commit `90973a80` implemented it as the opening root `AGENTS.md` "Subagents" bullet. The user later directed its removal on bloat grounds, and the original bullet — "Main session is manager; subagents execute work to keep main context clean" — was restored and is what `c369fd01` landed.

Root `AGENTS.md` "Diagnosis Discipline" ranks an explicit user statement above the approved plan, so the deviation is authorized and the user's instruction was trusted. The same rule requires the contradiction to surface as a residual naming both sides; that never happened. This document is that record.

## What survived

Root `AGENTS.md` steps 1-8 assign each mechanic to a role. They govern every tracked change, and for most mechanics the assignment names a performer without also forbidding main from performing it.

One assignment governs delegated review and audit work:

- Root `AGENTS.md:40` — every delegated review or audit is the `reviewer` role. Manually triggered parent/manager orchestrators such as `/next-plan-review` are not delegated reviewers: they remain in the parent and dispatch their own reviewer.

Four more bind only inside their own path or situation:

- Root `AGENTS.md:71` (step 2) — Tier-3 `/external-grill-plan` path: "main only adjudicates and interviews from those handoffs".
- `.agents/references/subagent-reporting.md:85-87` — liveness judging: the manager "never inspects raw task transcripts or logs".
- `.agents/skills/next-plan/SKILL.md:127-129` — the `## Primary advance` section: "main does not inspect or mutate repository or claim state."
- `.agents/skills/finalize-changes/SKILL.md:19-21` — finalization: "Main only adjudicates its concise handoffs, presents the exact summary and confirmation question, and resumes the worker after the user's answer."

The residual gap is therefore: main inspecting task repository evidence, editing tracked artifacts, building, and performing Plan-claim, Git, reconciliation, or landing mechanics carry no general prohibition — minus what the four path-scoped clauses above already cover inside their own situations (Tier-3 `/external-grill-plan`, liveness judging, `/next-plan` primary advance, `/finalize-changes` finalization), and minus delegated review or audit work assigned to reviewers. Manually triggered parent/manager orchestrators remain in the parent while dispatching their child reviewer.

## Design

Open decision for the user: does the manager boundary need any root-level statement, given that delegated reviews and audits are assigned to reviewers while manually triggered parent/manager orchestrators remain in the parent and dispatch their reviewer, and every other explicit prohibition is path-scoped?

- Outcome A — no statement needed. This record closes with the user's authority recorded.
- Outcome B — a statement is needed, in the root `AGENTS.md` "Subagents" bullet list, materially shorter than the removed bullet.

Outcome B must first be made decision-complete — exact authorized wording, tier, and checks — before it becomes actionable work.

The removed wording is not a candidate under either outcome. The user rejected it, and that rejection is the higher authority.

## Risk tier

Classify the outcomes separately:

- Outcome A — Tier 1. No implementation change to any tracked file; recording the answer and closing this record are this document's own lifecycle edit, not an implementation change.
- Outcome B — at least Tier 2. It changes a root workflow contract governing who may perform mechanics, which is not behavior-preserving documentation with no invariant exposure. Escalate if the resolved statement reaches Tier-3 mechanics or integration.

Outcome B's exact tier cannot be finalized until its boundary is resolved.

## Acceptance criteria

Recording the answer in this file is its own lifecycle edit under either outcome, separate from any implementation artifact; the criteria below count only implementation changes. Outcome A therefore completes this Plan without changing any other tracked file.

- The user's answer is recorded verbatim here before any edit elsewhere.
- Outcome A: this record closes with the answer recorded, and no tracked file other than this record changes.
- Outcome B: the outcome is made decision-complete first (exact wording, tier, checks), then root `AGENTS.md` carries one statement shorter than the removed bullet, with no tracked file other than root `AGENTS.md` and this record changed.

## Out of scope

- Re-adding the removed prose in any form or paraphrase.
- Editing `.agents/references/subagent-reporting.md`, `.agents/skills/next-plan/SKILL.md`, `.agents/skills/finalize-changes/SKILL.md`, or root `AGENTS.md` steps 1-8.
- Host-level enforcement, tool permissions, and role definitions under `.claude/agents/` or `.codex/agents/`.
- Changing what any mechanic does, as opposed to who may perform it.
