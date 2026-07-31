---
name: adversarial-review
description: >-
  Scoped fresh-eyes review that tries to disprove every artifact type changed
  by a Tier-3 change. Use automatically only for Tier-3 changes or when the root
  Change Workflow leaves one concrete reachable unresolved failure hypothesis
  after correctness review. Also use when the user asks to "attack this
  change", "assume it's broken", "find reasons this fails", or requests an
  adversarial second opinion on a supplied diff. Findings only; never edits.
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

1. For Tier 3, enumerate authorized hypotheses for every changed artifact type,
   grounded in the recorded Tier-3 triggers. Otherwise use the one unresolved
   hypothesis supplied by the caller. Define the smallest concrete trace that
   could prove or refute each.
2. Read the changed region and the callers, consumers, schemas, instructions,
   generated outputs, or sibling paths necessary for that trace. Diff-only
   reading is insufficient. Stop when the hypothesis dies or becomes proven.
3. Test the contract appropriate to the artifact. For code and shaders, trace
   logic, integration, lifetime, threading, determinism, edge states, and build
   reachability. For scripts, project metadata, schemas, and data, trace inputs,
   state changes, failure handling, compatibility, and consumers. For skills, plans,
   workflow, and documentation, trace discovery and invocation policy,
   executable instructions, authority boundaries, acceptance semantics, links,
   and contradictions with governing instructions.
4. Scale depth to the authorized risk. Do not replace a disproven hypothesis
   with unrelated edge cases merely to produce a finding. When all authorized
   hypotheses die, return PASS and stop.

## Evidence Rules

Every finding must cite a `file:line` actually read in this review and give a
reachable, in-scope, meaningful scenario: specific input or state leads to a
wrong outcome, violated governing contract, or failed approved acceptance
criterion. Drop suspicions that cannot establish reachability, scope, and
impact; do not soften them into optional advice.

Before reporting, try to refute the finding by checking alternate explanations,
guards, established preconditions, and governing invariants. Report only defects
introduced or newly exposed by the supplied change. Put proven pre-existing or
out-of-scope defects in `Residuals`; keep in-scope structural acceptance failures
as findings. Exclude style issues and diagnostics a prescribed compiler,
validator, or static check directly catches.

For any finding that depends on a non-obvious external API, language,
specification, or library claim, emit one verification request for that single
checkable statement, containing the symbol or rule, exact proposition, dependent proposed finding, applicable
version/configuration, and proposed official source. The caller routes each
request through `verify-external-claims`; do not present the claim as confirmed
until that verdict returns.

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

Status: PASS | NEEDS_ACTION | BLOCKED
Changed files: none
Decisive checks: <trace/read and result for each authorized hypothesis>
Build required: none
Residuals: <pre-existing defect, incomplete trace, pending external verdict, or none>
```

Use `NEEDS_ACTION` for findings or pending external verification and `BLOCKED`
only when required evidence could not be obtained. Critical means data loss,
broken functionality, determinism failure, or equivalent contract breach;
everything else reported is required, never optional.
