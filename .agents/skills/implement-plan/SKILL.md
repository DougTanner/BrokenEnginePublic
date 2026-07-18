---
name: implement-plan
description: >-
  Implement an assigned slice of an approved Broken Engine plan, then audit the
  implementation's assumptions in the same context before handing it off. Use
  for Change Workflow implementation subagents, including disjoint
  plan slices. Also use after making changes when asked "audit your
  assumptions", "what are you least confident about", or "what did you not
  verify"; in that case run the audit phase against the changes already made.
  Stops on plan/repository contradictions instead of improvising, fixes
  confirmed audit problems, and reports exact changed regions, affected-site triggers,
  reviewer focus areas, and residuals.
allowed-tools: [Read, Write, Edit, Glob, Grep, Agent, "Bash(git diff *)", "Bash(git status *)", PowerShell]
---

# Implement Plan

Implement only the assigned plan work, then audit the reasoning that produced
it while that reasoning is still available. Fresh-context reviewers can inspect
the diff, but cannot recover assumptions, unread dependencies, or silent plan
divergence from the implementing context.

## Inputs

Require these inputs for implementation mode:

- final approved plan document and explicit approved-delta summary (`none` is valid);
- assigned plan items and allowed file scope;
- the working checkout — a session worktree and fixed session-start commit for
  wrapper/queue sessions, the user-supplied checkout otherwise;
- manager execution-control record when one exists (Tier 3, queue,
  reconciliation, and landing work): risk tier and concrete triggers, required
  and conditional roles, and the initial acceptance matrix;
- applicable repository instructions;
- concise caller summary of relevant findings, approved decisions, scope, and reviewer focus;

## Reporting Mode

Return the Handoff Report inline. Preserve changed regions, affected-site
triggers, reviewer focus areas, and residuals, but do not create an
intermediate evidence artifact.

If invoked directly after changes were already made, treat it as audit-only
mode: derive the touched scope from the caller's change list, edit history, and
the diff against the supplied fixed baseline. Do not claim unrelated or
pre-existing worktree changes.

## Phase 1: Implement Assigned Work

0. Confirm the user-approved execution card, risk trigger, scope, and acceptance
   checks are still current. Stop only on a material contradiction or unresolved
   decision.
1. Read the assigned plan items, applicable `AGENTS.md` files, and the current
   implementation before editing. Search for existing helpers and mirrored
   patterns so the change follows repository structure without duplicating
   logic.
2. Implement the smallest complete change that satisfies the assignment. Stay
   within the named plan slice and file scope; leave unrelated cleanup alone.
3. Treat a contradiction between the approved plan and repository reality as a
   residual. Stop that item, quote the conflicting assumption and repository
   evidence, and do not invent a replacement design. Continue only with other
   independent assigned items that remain valid.
4. Follow any specialist collection, shader, project, or subsystem skill whose
   trigger applies. Keep generated or mechanical edits within the same assigned
   scope.
   When nested delegation is required, give it a concise self-contained scope,
   approved decisions, and the relevant inline handoff.
5. Verify the implementation in proportion to the slice. Run focused static
   checks needed to establish that the edit is internally coherent. Do not
   substitute these checks for the manager's targeted compile/static-check stage,
   which runs after all required propagation and before correctness review.
   Delegate any implementation-assigned build to a Sonnet `/compile` subagent and
   preserve its status plus error and warning lines verbatim; leave broader
   review and runtime verification to their trigger-based stages.
6. Track every file changed and the exact functions, types, sections, or data
   regions touched. Emit an affected-site trigger for every sweep handoff or
   signature, identity, semantics, layout, client/server guard-affinity, or
   mirrored-pattern change. Name the symbol/pattern and search scope. If none
   applies, emit explicit `none`; `/update-affected-code` still runs for any
   code change and verifies that absence.
7. For checks that touch ignored or non-worktree state, follow the owning
   subsystem contract and report the exact path, owner/serialization mechanism,
   expected persistence, and outcome. This includes worktree build outputs,
   Gaea agent cache, DataPacker cache, `.pack`/`.manifest` outputs, and ignored
   checkout caches; do not invent cleanup or persistence rules outside that owner.

Do not enter implementation mode for an audit-only invocation. Start at the
self-audit phase and limit fixes to problems confirmed in the existing changes.

## Phase 2: Same-Context Self-Audit

Run this phase before returning from the implementing context. Do not delegate
it: the purpose is to expose the assumptions and incomplete knowledge held by
the context that made the changes.

List at most 3–7 items across the rubric, ranked by severity. Zero findings in
a dimension is valid; do not manufacture doubts. Include only correctness or
requirement risks, not style, polish, or hypothetical future concerns.

Make every item falsifiable:

- **Claim**: identify the concrete assumption or possible failure.
- **Check**: name the read, search, trace, build, or runtime evidence that would
  confirm or refute it.
- **Result**: state what the check actually showed.

Do not use confidence percentages or vague uncertainty. Turn each plausible
way the change could be wrong into a concrete check, then investigate every
checkable item inline to root cause. Fix confirmed problems through the normal
edit flow and repeat the check. Hand off only items that genuinely require
runtime behavior, user knowledge, hardware, or a later independent reviewer.

Audit each relevant dimension:

1. **Unverified assumptions**: calling context, threading and dispatch safety,
   lifetime and ownership, value ranges, frame phase, client/server reachability,
   deterministic state, and data layout.
2. **Unread dependencies**: callers, callees, producers, consumers, and shared
   definitions the implementation relied on without reading.
3. **Stale understanding**: touched regions read before later edits; re-read the
   current file rather than relying on memory.
4. **Memory-sourced symbols**: APIs, members, constants, and patterns written
   from recollection; confirm their definitions and signatures in the repo.
5. **Plan divergence and scope drift**: compare the changed files and regions
   against the approved assignment. Use the fixed session-start commit plus edit
   history, not a moving merge base, so integrated or pre-existing work is not
   misattributed.
6. **Partial sweeps**: verify repeated-pattern changes by search, especially
   renames, signatures, collection allocation/copy/log/spawn/transfer wiring,
   and mirrored client/server code.
7. **Premortem**: identify and check the smallest remaining issue that could
   crash, corrupt, or desynchronize the running game.
8. **Pasted logic**: search distinctive lines from substantial new blocks for
   existing helpers or accidental duplication. Deliberate mirrored boilerplate
   remains exempt.

If an audit fix changes C++, have a Sonnet subagent invoke `/compile` to
selectively rebuild every affected `.cpp` file and return status plus error and
warning lines verbatim. Repair only errors introduced by that fix. This keeps a
late self-audit correction from reaching fresh-context reviewers uncompiled. If
the build cannot run, report the exact blocker as a reviewer focus area and
residual; never imply that compilation passed.

## Handoff Report

Return concise evidence, not a narrative summary:

```markdown
Implementation:
- `path` — function/type/section: completed change

Self-audit resolved:
- Claim → check → result; fix and repeated verification if applicable
- none

Affected-site triggers:
- `sweep | signature | identity | semantics | layout | guard | mirror` — exact
  symbol/pattern and scope `/update-affected-code` must verify
- none

Reviewer focus areas:
- specific uncheckable request phrased as "verify X holds when Y"
- none

Files changed + functions/regions touched:
- `path` — function/type/section
- none

Residuals:
- incomplete item, skipped fix, contradiction, or unverified blocker with evidence
- none
```

List each changed file once and identify every touched region, or report `none`.
Preserve affected-site triggers verbatim so the caller can either invoke
`/update-affected-code` as mandatory work or record it N/A, and preserve audit
concerns for every triggered fresh-context review. The `Residuals` section
is the final footer even when another invoked skill prescribes its own output
template.
