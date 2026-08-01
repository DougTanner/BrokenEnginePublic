---
name: scope-review
description: >-
  Review one Tier-2+ change's whole session diff for scope authorization and
  diff-observable minimality. Use once per review round during Change Workflow
  Step 5; never per artifact type and never for Tier-1 work. Findings only;
  never edits.
allowed-tools: [Read, Grep, Glob, PowerShell]
---

# Scope Review

Verify that the change is exactly what was authorized — no more — so the
correctness reviewers can ignore scope entirely. Scope authorization findings
and findings of unnecessary extra work belong to this skill alone.

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

Run in the delegated execution context of
`../../references/subagent-reporting.md`, dispatched via `/codex-review`;
inline review is prohibited. Dispatch is once per review round over the whole
diff; after the manager accepts findings and fixes land, only the affected
regions receive a focused scope re-review.

## Review

1. Take the changed regions from the read-only inventory instead of re-deriving
   hunks: `pwsh -NoProfile -File
   .agents/scripts/Get-SessionChangeInventory.ps1 -RepositoryRoot <absolute
   repository toplevel> -Baseline <full 40-character SHA> -Regions` (add `-Head
   <commit>` for a committed head; in Claude Code's Git Bash terminal convert
   the script path and root with `cygpath -w` exactly as
   `../cleanup-worktrees/SKILL.md` shows). It writes no file and prints one
   `broken-engine-session-change-inventory/v1` object whose `regions` table
   holds one row per hunk with `path`, `kind`, new and old line ranges, and the
   enclosing `symbol`, alongside `entries` and their `class` values. Only
   `status` `pass` (exit 0) is usable; `blocked` (exit 2) or `error` (exit 1)
   means the diff input is unavailable, so the `## Inputs` `BLOCKED` rule
   applies. The table is capped at 400 rows, so read `truncation.regions` for
   the true `full` count and the `emitted` count and treat `truncated` `true` as
   coverage of the emitted rows only. An untracked file appears only when the
   caller supplies it with `-IncludeUntracked <comma-separated paths>`, and
   `counts.unlistedUntracked` reports how many untracked files the run did not
   list. Never enumerate these regions inline.
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
Regions checked: <emitted>/<full>
Findings: <count or none>
Status: PASS | NEEDS_ACTION
```

`Regions checked:` reports the `-Regions` table's row counts from
`truncation.regions` as `emitted/full`. When they differ, only the emitted rows
were reviewed: say so and name the unreviewed remainder rather than claiming the
full count was checked, and the overall `Status` is then never `PASS` — at best
`NEEDS_ACTION` with the shortfall stated, because unreviewed regions cannot be
cleared.

The manager decides each finding on whether the failure is concrete,
reachable, and meaningful under the standard defaults; this review adds no
extra rounds.
