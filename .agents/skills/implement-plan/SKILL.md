---
name: implement-plan
description: >-
  Dispatch and perform one assigned slice of an approved Broken Engine plan,
  followed by a same-context audit of the implementation assumptions. Use for
  Change Workflow implementation work, including disjoint slices, and when the
  worker that made existing changes is asked to audit its assumptions.
allowed-tools: [Read, Write, Edit, Glob, Grep, "Bash(git diff *)", "Bash(git status *)", PowerShell]
---

# Implement Plan

The main session reads this skill, then dispatches exactly one `implementer`
with a self-contained brief and no inherited conversation context — fresh on
Claude, `fork_turns: "none"` on Codex, per the authoritative default in
`../../references/subagent-reporting.md`. That worker performs both
implementation and its same-context assumption audit. If already running as the
assigned worker, do not dispatch again. Workers never delegate; return any
separate-role work to the manager.

## Required Brief

Require the authoritative task-brief fields
(`../../references/subagent-reporting.md`) plus these skill-specific fields, in
both implementation and audit-only modes:

- mode: `implementation` or `audit-only`;
- the final approved plan and the exact changes the user approved after it
  (`none` is valid), plus the assigned items and allowed file scope;
- session baseline used for all attribution;
- pre-existing ownership snapshot naming every already changed or untracked
  path and its owner/outcome (`none` is valid);
- risk triggers and reviewer focus;
- execution card when one exists.

Do not infer a missing session baseline or ownership snapshot from a moving merge base
or the current status. Return the missing input to the manager. In audit-only
mode, continue only in the context that made the named changes; derive its
touched regions from edit history and the session-baseline diff while excluding
the ownership snapshot.

## Phase 1: Implement

Skip this phase in audit-only mode.

1. Confirm the execution card, scope, decisions, and acceptance checks remain
   current. Read the assigned plan items, applicable `AGENTS.md` files, current
   implementation, dependencies, helpers, and mirrored patterns before edits.
2. Before introducing a new function, type, helper, or substantial logic block,
   search with `rg` for the proposed identifier, responsibility terms describing
   the behavior, the closest sibling implementation, and direct callers or
   consumers. Read plausible matches before deciding new code is necessary. Reuse
   an existing mechanism when it satisfies the current contract. If a plausible
   match does not fit, establish the concrete mismatch in the implementation
   reasoning; include it in the handoff only when the rejected candidate or
   resulting duplication is non-obvious to a reviewer. Repository facts remain
   resolvable through continued read-only inspection. If ownership, invariant
   exposure, or the spread of breakage in shared code remains meaningfully uncertain after the
   targeted searches, do not edit the affected shared region; return the exact
   unknown as a residual for manager resolution, with user involvement when
   necessary.
3. Implement the smallest complete assigned change. Preserve all snapshotted
   pre-existing work and leave unrelated cleanup alone.
4. If plan and repository reality meaningfully conflict, stop that item. Quote
   the plan assumption and repository evidence as a residual; never improvise
   a replacement design. Continue only independent valid items.
5. Apply compatible specialist instructions in this same context. If a
   specialist requires another role, return the requirement to the manager
   instead of invoking it.
6. Run only focused reads, searches, traces, and static checks needed for
	internal coherence. Return compilation, runtime checks, harness work, and
	independent reviews to the manager for dispatch to their assigned roles.
7. Record each changed file and exact function/type/section. For code, emit a
   note that another code site may be affected for each signature, identity,
   semantics, layout, client/server guard, missed-caller, or mirrored-code
   concern, including symbol/pattern and search scope. Code always returns
   propagation work to `/update-affected-code`, even when the note is
   `none found`; propagation is N/A only when no code changed.
8. For ignored or non-worktree state, follow its owner contract and report the
   exact path, persistence mechanism, expected persistence, and result.

## Phase 2: Same-Context Audit

The worker that made the changes performs this phase before returning. It never
substitutes for domain, adversarial, or session review.

List zero to seven correctness or requirement risks, ranked by severity. Each
item is checkable: Claim names the possible failure, Check names a
read/search/trace/static check, and Result states the evidence. Investigate
every inline-checkable item, fix confirmed in-scope problems, and repeat its
check. Hand off anything requiring compilation, runtime behavior, hardware,
user knowledge, or an independent role.

Cover relevant dimensions without manufacturing doubts: calling context,
threading, lifetime, ranges, phases, reachability, determinism, layout, and
ownership; unread callers, callees, producers, and consumers, and symbols
recalled rather than read; the current file reread after edits; plan and scope
attribution against the session baseline and ownership snapshot; partial sweeps
and mirrored client/server or collection wiring; and the smallest plausible
crash, corruption, or desync path.

Audit fixes remain inside the assignment. Re-run applicable static checks and
update affected-site and build handoffs; never claim compilation or runtime
verification passed.

## Handoff

Return the concise delegated-reporting handoff with these required fields:

```text
Status: PASS | NEEDS_ACTION | BLOCKED
Changed files: <path — exact functions/types/sections, or none>
Decisive checks: <read/search/trace/static command and result>
Self-audit resolved: <Claim -> Check -> Result; fix/recheck, or none>
Affected-site triggers: <kind — symbol/pattern and search scope, or none found>
Propagation required: /update-affected-code — <code scope> | N/A — no code changed
Build required: <target, configuration/platform, selected project-member .cpp;
  for headers, consuming targets and configuration/platform; or none>
Reviewer focus areas: <verify X holds when Y, or none>
Residuals: <contradiction, incomplete item, or blocker with evidence, or none>
```

`Residuals` stays last. Name each changed file once. Build requests must be
executable without rediscovery: each changed `.cpp` names its exact target,
configuration/platform, and selected project-member path; each changed header
names every consuming target and configuration/platform. The manager dispatches
the assigned build/runtime role and routes its concise result or later fix work.
