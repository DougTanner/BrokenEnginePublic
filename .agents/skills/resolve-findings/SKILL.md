---
name: resolve-findings
description: >-
  Resolve an explicitly accepted review finding, compile failure, or runtime
  failure within the Broken Engine Change Workflow. Use for delegated fix work
  after the manager supplies fixed evidence, intent classification, scope, and
  baseline. Confirms root cause before editing, applies only a non-structural
  conformance fix, checks affected sites, and returns exact regions, build
  requirements, external-claim requests, and residuals.
allowed-tools: [Read, Grep, Glob, Edit, "Bash(git diff *)", "Bash(git status *)", PowerShell]
---

# Resolve Findings

Fix only the accepted failures assigned by the manager. Do not delegate. The
manager owns adjudication, scope changes, builds, independent verification, and
all fresh-context review.

## Required Assignment

Require a self-contained assignment containing:

- accepted finding or failure evidence and its prescribed check;
- classification: intent `conformance` or `plan_delta`, and scope
  `non_structural` or `structural`;
- approved plan or concise intended behavior;
- assigned files/functions, fixed baseline, and pre-existing ownership
  snapshot;
- applicable repository instructions, required verification, and known build
  target/configuration when relevant.

Reconstruct a missing detail only when the assignment and worktree make it
unambiguous. Otherwise do not edit; report the missing input as a residual.

Accept only `conformance + non_structural`. Report
`PLAN DELTA REQUIRED: yes` without editing when the correction would change
approved behavior, scope, acceptance criteria, or verification obligations.
Structural work also returns to the manager: an in-scope acceptance failure
blocks the active change; proven pre-existing or out-of-scope work may become a
follow-up; user-approved expanded scope re-enters `/implement-plan` after the
manager updates the canonical plan.

## Fix Workflow

For each assigned item:

1. State a falsifiable suspected root cause. Read the failing region and enough
   callers, callees, logs, or sibling paths to distinguish cause from symptom.
   A failing expression alone does not establish the originating cause.
2. Confirm the cause from direct repository inspection or supplied failure
   evidence before editing. If evidence proves a different in-scope cause,
   record the correction. If it remains uncertain, conflicts with controlling
   intent, requires user judgment, or lies outside scope, leave it unchanged.
3. Apply the smallest change restoring approved behavior. Do not refactor,
   clean adjacent code, or fix unassigned failures.
4. Re-read every fixed region and directly affected path. Demonstrate why the
   original scenario no longer follows. Run focused static checks available in
   this context; return builds, runtime checks, harness work, and independent
   verification to the manager.
5. For a one-function code fix with no signature or contract change, scan its
   callers, mirrored client/server or collection patterns, shared headers, and
   stale comments. Fix only candidates inside scope and report every outside
   candidate. Otherwise emit affected-site triggers for manager propagation,
   including symbol/pattern and search scope.
6. Audit the completed edit against the assignment, baseline, ownership
   snapshot, and smallest plausible regression. Fix confirmed in-scope defects
   and repeat their checks. Do not claim compilation or runtime success.

Use this authority order when sources conflict: explicit user statement, final
approved plan and deltas, repository instructions/docs/comments, then current
behavior. Name the contradiction and controlling source.

Do not establish disputed external API, language, specification, or library
behavior from memory. Emit one stable atomic request per proposition: name the
symbol/rule, exact proposition, dependent item, applicable version and local
configuration, and candidate official source. The manager routes it to
`/verify-external-claims`; keep the item unresolved pending that verdict.

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
Reviewer focus areas: <condition the manager's verifier must falsify, or none>
Residuals: <unresolved/out-of-scope item, evidence, and next owner/action, or none>
```

Keep `Residuals` last. Name each changed file once. A requested build is manager
work, not a passed check. The manager dispatches independent verification as a
separate role after the fix and required checks complete. Use `PASS` when every
assigned item is fixed with no fix-work residual, `NEEDS_ACTION` when manager
action remains, and `BLOCKED` when missing required evidence prevents work.
