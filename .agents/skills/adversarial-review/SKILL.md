---
name: adversarial-review
description: >-
  Scoped fresh-eyes review that tries to disprove a Tier-3 change across every
  changed artifact type. Use automatically only for Tier-3 changes, when
  correctness review leaves one concrete reachable unresolved failure
  hypothesis, or when the user requests an adversarial second opinion on a
  supplied diff. Findings only; never edits.
allowed-tools: [Read, Grep, Glob, PowerShell]
---

# Adversarial Review

Run in one fresh delegated `reviewer`. Do not edit files, run state-changing commands,
implement fixes, or delegate further. Review logic and correctness; leave style
to the artifact's domain review.

## Inputs

Require the implementation handoff and complete changed-artifact list; plan or
intent with declared invariants; approved Tier-3 triggers (see root AGENTS.md, Risk tiers) or the exact unresolved
reachable hypothesis; and relevant prior findings, residuals, and focus areas.

Whenever a session baseline exists, the complete changed-artifact list is the
`entries` rows and their `class` values from the read-only inventory: `pwsh
-NoProfile -File .agents/scripts/Get-SessionChangeInventory.ps1 -RepositoryRoot
<absolute repository toplevel> -Baseline <full 40-character SHA>` (add `-Head
<commit>` for a committed head; in Claude Code's Git Bash terminal convert the
script path and root with `cygpath -w` exactly as
`../cleanup-worktrees/SKILL.md` shows). It writes no file and prints one
`broken-engine-session-change-inventory/v1` object with `entries`, `counts`, and
`triggers`. Only `status` `pass` (exit 0) is usable; `blocked` (exit 2) or
`error` (exit 1) means the changed-artifact list is unavailable, so return
`BLOCKED`. The list is capped at 500 rows, so read `truncation.entries` and
`truncated`: an emitted count below the full count also means the complete
changed-artifact list is unavailable, so return `BLOCKED` for it exactly as for
a non-pass status. An untracked file appears only when the caller supplies it
with `-IncludeUntracked <comma-separated paths>`, and `counts.unlistedUntracked`
reports how many untracked files the run did not list. Never enumerate the
changed artifacts inline.

For a direct user request about a supplied diff, treat the supplied intent,
declared invariants, and behavioral or contract statements expressed by the diff as
the authorized hypotheses. If no briefing exists, reconstruct these inputs from
conversation history. Do not turn either case into an open-ended repository audit.

## Method

Read the callers, consumers, schemas, instructions, generated outputs, or
sibling paths each hypothesis needs; diff-only reading is insufficient.

Test the contract appropriate to the artifact. For code and shaders, trace
logic, integration, lifetime, threading, determinism, edge states, and build
reachability. For scripts, project metadata, schemas, and data, trace inputs,
state changes, failure handling, compatibility, and consumers. For skills, plans,
workflow, and documentation, trace discovery and invocation policy,
executable instructions, authority boundaries, acceptance semantics, links,
and contradictions with governing instructions.

## Evidence Rules

Every finding cites a `file:line` read in this review, names a reachable in-scope
failure the change introduced or newly exposed (input or state leads to a wrong
outcome, a violated governing contract, or a failed approved acceptance
criterion), and survives an attempt to refute it against guards, established
preconditions, and governing invariants. Report proven pre-existing or
out-of-scope defects in `Residuals` instead of fixing or expanding into them,
while in-scope structural acceptance failures stay findings. Exclude style issues
and diagnostics a prescribed compiler, validator, or static check directly
catches.

For any finding that depends on a non-obvious external API, language,
specification, or library claim, emit one single-claim request per
`/verify-external-claims` (`../verify-external-claims/SKILL.md`,
`## External Claim Requests`); a pending verdict makes the review
`NEEDS_ACTION`, and the claim is not confirmed until that verdict returns.

## Output

```markdown
## Adversarial Review Results

### Findings
- `path:line` — **Critical:** or **Required:** — <input/state -> wrong outcome that matters, and why>
- none

### API Verification Requests
- <symbol/rule> — <exact proposition> — <dependent proposed finding> — <applicability> — <official source>
- none

### Traced Clean
<Only when there are no findings: hypotheses traced, decisive refutation, and
`PASS — disproof attempt complete; stop.`>
```

Close with the shared handoff lines (`../../references/subagent-reporting.md`,
`## Handoffs`).

Use `NEEDS_ACTION` for findings or pending external verification and `BLOCKED`
only when required evidence could not be obtained. Critical means data loss,
broken functionality, determinism failure, or equivalent contract breach;
everything else reported is required, never optional.
