---
name: scope-review
description: >-
  Review one Tier-2+ change's whole session diff for scope authorization and
  diff-observable minimality: every changed region must trace to a named plan
  `## In scope` clause or user instruction, and overbuilt additions — unused
  options, speculative paths, one-use indirection, unrequested backward
  compatibility — are findings. Use once per review round during Change Workflow
  Step 5, dispatched through /codex-review in parallel with the per-artifact
  correctness reviews; never per artifact type and never for Tier-1 work.
  Findings only; never edits.
allowed-tools: [Read, Grep, Glob, PowerShell]
---

# Scope Review

Verify that the change is exactly what was authorized — no more — so the
correctness reviewers can ignore scope entirely. Scope authorization and
gold-plating findings belong to this skill alone.

## Inputs

- The whole session diff against the session baseline (full Git SHA), covering
  every changed artifact type at once. Never accept a per-artifact slice.
- The authorization source: the approved plan's `## In scope` and
  `## Out of scope` sections, or the explicit user-instruction list for
  unplanned work; the execution card when one exists.
- Current residuals or focus notes from the manager.

If the diff, session baseline, or authorization source is missing, return `BLOCKED`
naming the missing input.

## Execution Context

Run solely inside one delegated `reviewer` via `/codex-review`; inline review
is prohibited. Read-only: never edit a file. Tool restrictions are prose
boundaries because frontmatter must not alter the calling context. Dispatch is
once per review round over the whole diff; after the manager accepts findings
and fixes land, only the affected regions receive a focused scope re-review.

## Review

1. Enumerate changed regions from the diff — per hunk, per function or section
   touched.
2. Authorization pass: map each region to the `## In scope` entry or user
   instruction that authorizes it, counting the mechanical necessities the
   named change requires (includes, declarations). An unmapped region is an
   `unauthorized` finding; a region matching an `## Out of scope` entry is
   likewise `unauthorized`.
3. Minimality pass over added bytes only: flag an unused option, a speculative
   path with no current consumer, one-use indirection with no required
   contract, or backward-compatibility code the authorization source did not
   request.
4. KISS pass, diff-observable only: flag complexity visible in the diff itself
   that a plainly simpler form of the same authorized change avoids. Do not
   hunt the repository for simplifications.
5. Precision guard: every finding cites the specific clause violated or states
   the specific authorization that is absent. No clause named, no finding.

## Exclusions

- DRY/reuse — `/plan-audit` before implementation and `/repo-code-review`'s
  proven-helper rule.
- Correctness, style, and formatting — the per-artifact Step-5 reviews,
  `/code-style-review`.
- Landing-gate authorization reconciliation — `/verify-changes`.
- Retrospective minimality grading of landed work — `/next-plan-review`.

## Output

One line per finding:

```text
region (file:lines) | class: unauthorized|overbuilt|kiss | cited clause or absent authorization | evidence
```

Example:

```text
Engine/Source/Audio/Mixer.cpp:88-104 (RetryCount option) | class: overbuilt | no In-scope clause requests retry configuration | option is never read
```

Then the summary block:

```text
Baseline: <full SHA>
Authorization source: <plan path or user-instruction identifier>
Regions checked: <count>
Findings: <count or none>
Status: PASS | NEEDS_ACTION
```

The manager adjudicates each finding for concrete reachable failure and
materiality under the standard defaults; this review adds no extra rounds.
