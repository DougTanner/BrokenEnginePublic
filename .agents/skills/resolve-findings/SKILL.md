---
name: resolve-findings
description: >-
  Resolve an explicitly accepted review finding, compile failure, or runtime
  failure within the Broken Engine Change Workflow. Use for delegated fix work
  after the manager supplies fixed evidence, intent classification, scope, and
  session baseline.
allowed-tools: [Read, Grep, Glob, Edit, "Bash(git diff *)", "Bash(git status *)", PowerShell]
---

# Resolve Findings

Run inside one delegated `implementer`. Fix only the accepted failures assigned
by the manager and do not delegate. The manager owns finding decisions and scope
changes, and dispatches builds, independent verification, and all fresh-context
review to their assigned roles.

## Required Assignment

Require a self-contained assignment carrying the authoritative task-brief fields
(`../../references/subagent-reporting.md`) plus these skill-specific fields:

- accepted finding or failure evidence and its prescribed check;
- classification: intent `conformance` or `plan_delta`, and scope
  `non_structural` or `structural`;
- assigned files/functions, session baseline, and pre-existing ownership
  snapshot;
- known build target/configuration when relevant.

Reconstruct a missing detail only when the assignment and worktree make it
unambiguous. Otherwise do not edit; report the missing input as a residual.

Accept only `conformance + non_structural`. Report
`PLAN DELTA REQUIRED: yes` without editing when the correction would change
approved behavior, scope, acceptance criteria, or verification obligations.
Structural work also returns to the manager: an in-scope acceptance failure
blocks the active change; proven pre-existing or out-of-scope work may become a
follow-up; user-approved expanded scope re-enters `/implement-plan` after the
manager updates the authoritative plan.

## Fix Workflow

Confirm a checkable root cause before editing; leave an item unchanged when the
cause is uncertain or out of scope. Apply the smallest change restoring approved
behavior, check affected sites, and return out-of-scope candidates and build or
runtime verification to the manager. Before handing off, audit the completed
edit against the assignment, session baseline, and smallest plausible
regression, and report the result under `Self-audit resolved`.

Resolve conflicting sources by the authority order in root `AGENTS.md`
`### Diagnosis Discipline`, naming the contradiction and the controlling source.

Do not establish disputed external API, language, specification, or library
behavior from memory: emit one single-claim request per
`/verify-external-claims` (`../verify-external-claims/SKILL.md`,
`## External Claim Requests`). A pending verdict keeps the item unresolved and
the handoff `NEEDS_ACTION`.

## Handoff

Return one compact item table followed by handoff prose:

```markdown
| Item | Result | Confirmed root cause and evidence | Fixed region | Focused check |
|---|---|---|---|---|
| <item> | FIXED or UNRESOLVED | <file:line or log evidence> | <region or none> | <check and result> |

Status: PASS | NEEDS_ACTION | BLOCKED
Changed files: <path — exact functions/types/sections, or none>
PLAN DELTA REQUIRED: no | yes — <reason and manager action>
Decisive checks: <read/search/trace/static command and result>
Self-audit resolved: <Claim -> Check -> Result; fix/recheck, or none>
Affected-site triggers: <kind — symbol/pattern and search scope, or none found>
Propagation required: /update-affected-code — <code scope> | N/A — no code changed
Build required: <target, configuration/platform, selected project-member .cpp;
  for headers, every consuming target and configuration/platform; or none>
External/API verification requests: <symbol/rule — proposition — dependent item
  — version/configuration — candidate official source, or none>
Reviewer focus areas: <condition the independent verifier must try to disprove, or none>
Residuals: <unresolved/out-of-scope item, evidence, and next owner/action, or none>
```

Keep `Residuals` last. Name each changed file once. A requested build is
`builder` work dispatched by the manager, not a passed check. The manager
dispatches independent verification as a separate role after the fix and
required checks complete. Use `PASS` when every
assigned item is fixed with no fix-work residual, `NEEDS_ACTION` when manager
action remains, and `BLOCKED` when missing required evidence prevents work.
