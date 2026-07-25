---
name: plan-audit
description: >-
  Adversarially audit a Tier-2 or Tier-3 implementation plan before
  implementation; for Tier 3 it runs before /external-grill-plan. Do not add it
  to Tier-1 mechanical changes.
  Runs inside one delegated `reviewer`; findings only, with no edits, harness
  work, user interview, or further delegation.
allowed-tools: [Read, Grep, Glob, PowerShell]
---

# Plan Audit

Use this skill for every Tier-2 and Tier-3 change; Tier-1 mechanical work
skips it. Assume the supplied plan is flawed, verify it against the current
repository, and return only concrete findings and improvement suggestions for
the calling session to resolve when user input is actually needed — through
`/external-grill-plan` for Tier 3, or directly with the user for Tier 2. The
audit is findings-only work and never creates an approval gate; a `/next-plan`
invocation additionally follows the canonical execution-gate contract
(`../next-plan/references/execution-gates.md`).

## Inputs and Snapshot

- Immutable complete plan supplied inline, by exact file path, or as the exact
  claimed-plan revision. Inline plans require a stable snapshot identifier and
  stable heading IDs so findings can cite `<snapshot>#<heading-id>`.
- Draft execution-control record for every Tier-2 and Tier-3 plan: proposed
  tier and triggers, roles, and each acceptance criterion with its decisive
  check, expected result, and independent signal when a check is duplicated.
- Execution card and fixed session baseline only when root `AGENTS.md` triggers
  them (Tier 3, queue, reconciliation, or landing work).
- User intent and applicable repository instructions
- Relevant repository paths and every cited code region
- Accumulated constraints or known residuals

Load file or claimed-plan bytes once and retain that immutable snapshot for the
whole audit. Do not follow later edits. If an input above is missing, return
`BLOCKED` with the exact missing input rather than auditing a moving draft.

## Reporting Mode

Return the audit inline as the next role's decision input, not final evidence.
If constrained, narrowed, or interrupted, return findings gathered so far in
the standard format immediately; never return silence.

## Execution Context

Run inside one delegated `reviewer`; only explicit user direction permits an
inline run, which `/verify-changes` records as `inline`. Tool restrictions are
prose boundaries because frontmatter must not alter the calling context.

## Audit

1. Read the complete plan, applicable `AGENTS.md` files, and every cited code
   region. Anchor every search to a plan claim: callers, mirrors, cited-type
   headers, or affected-site hunts. Do not explore unrelated plans/subsystems.
2. Verify structural assumptions, call sites, mirrored client/server paths, ownership, data layout, frame phase, threading, determinism, serialization, build wiring, and runtime verification where relevant.
3. Hunt unresolved options, hidden behavior changes, contradictions, magic defaults, undeclared invariant exposure, missing affected locations, ungrounded requirements or checks, scope that duplicates an existing mechanism, and any file the plan's own `## Coordination` section obliges the implementer to edit yet its scope contract omits.
4. Ground corrections in user intent, repository contract, or necessary
   integration. Prefer reuse, narrower scope, missing propagation/verification,
   or replacement of an invalid step; otherwise report the user decision.
5. Scale depth to the plan. Keep a one-file refactor light; inspect a new subsystem across its full integration surface.
6. Build bidirectional traceability. Every requirement and exposed invariant
   must map to concrete implementation steps/sites and decisive checks; every
   implementation step/site and check must map back to a stated requirement or
   invariant. Report missing links, orphan work, and checks that cannot observe
   their claimed outcome.
7. For a `/next-plan` invocation, compare the complete current plan to the
   execution card and fixed session baseline. Verify that its goal, out-of-scope
   boundary, Tier-3 trigger, interfaces and invariants, acceptance checks with
   expected observations, role dispositions, and unresolved decisions agree
   with current repository evidence. Report any mismatch as a finding for the
   calling session; do not manufacture authority artifacts or another approval
   gate.
8. Audit the execution-control proposal against the risk-tier definitions in
   root `AGENTS.md`, classifying at the highest applicable tier. Verify that every
   concrete trigger is named, required and conditional roles fit the actual file
   types and risks, and each acceptance criterion has an initially decisive check
   and expected result, with a named independent signal for any duplicate check.
   If evidence proves a criterion unverifiable in the available environment,
   report a must-fix finding with an achievable replacement or a material user
   decision; do not defer it to an end-of-session waiver.
   Route changed C++ to `/repo-code-review`, changed GLSL to `/glsl-review`,
   and a shared CPU/GLSL dual-language header to both. Do not route
   documentation, workflow, style-only, or project-membership-only bytes to
   `/repo-code-review`. A reviewer may recommend escalation with evidence but
   may not silently lower the tier.

When a proposed finding depends on a non-obvious external API, language,
specification, or library fact, do not treat it as established. Emit one atomic
request per proposition with a stable ID:

```text
Claim ID: PA-EXT-###
Proposition: <one fact that can be VERIFIED, REFUTED, or UNRESOLVED>
Applicability: <version, target, flags, extensions, or constraints>
Dependent finding: <PA-F-### and why the verdict matters>
Candidate official source: <URL or exact upstream identifier, if known>
```

The manager runs `/verify-external-claims` on each packet — which itself
dispatches the `locator` that gathers evidence — then adjudicates the dependent
finding. Pending requests make the audit `NEEDS_ACTION`, not a confirmed
finding.

Do not edit any repository file, run `/agent-harness`, interview the user, or
spawn another agent. The calling session owns all judgment. After it
adjudicates findings and external verdicts, every Tier-3 result, including a
clean pass, proceeds to `/external-grill-plan`; Tier 2 returns to the manager.

## Output

For each finding:

> `PA-F-###` — `plan-path:line` or `snapshot#heading-id` — category — concrete problem — evidence: `repository-path:line` — proposed improvement

If clean, state `PASS — no material plan flaws found.` Return:

```text
Plan snapshot: <immutable identifier>
Findings: <entries or none>
API Verification Requests: <atomic packets or none>
Traceability checked: <requirements/invariants <-> implementation sites/checks>
Required next step: Tier 3 -> manager adjudication, then /external-grill-plan | Tier 2 -> manager adjudication
Status: PASS | NEEDS_ACTION | BLOCKED
Changed files: none
Decisive checks: <read/search/trace and result>
Build required: none
Residuals: <incomplete audit item, pending external verdict, or none>
```

Use `NEEDS_ACTION` for findings or pending external verdicts and `BLOCKED` only
when required input or evidence is unavailable.
