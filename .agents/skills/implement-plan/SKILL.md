---
name: implement-plan
description: >-
  Dispatch and perform one assigned slice of an approved Broken Engine plan,
  followed by a same-context audit of the implementation assumptions. Use for
  Change Workflow implementation work, including disjoint slices, and when the
  worker that made existing changes is asked to audit its assumptions. Requires
  a fixed baseline and pre-existing ownership snapshot, stops on
  plan/repository contradictions, and reports affected-site, build, review,
  and residual handoffs to the manager.
allowed-tools: [Read, Write, Edit, Glob, Grep, "Bash(git diff *)", "Bash(git status *)", PowerShell]
---

# Implement Plan

The main session reads this skill, then dispatches exactly one `implementer`
with a self-contained brief and no inherited conversation context — fresh on
Claude, `fork_turns: "none"` on Codex, per the canonical default in
`../../references/subagent-reporting.md`. That worker performs both
implementation and its same-context assumption audit. If already running as the
assigned worker, do not dispatch again. Workers never delegate; return any
separate-role work to the manager. The host mapping is restated in
`references/client-compatibility.md`.

## Required Brief

Require the canonical bounded brief fields
(`../../references/subagent-reporting.md`) plus these skill-specific fields, in
both implementation and audit-only modes:

- mode: `implementation` or `audit-only`;
- final approved plan and explicit approved deltas (`none` is valid), plus the
  assigned items and allowed file scope;
- fixed session-start commit used for all attribution;
- pre-existing ownership snapshot naming every already changed or untracked
  path and its owner/disposition (`none` is valid);
- risk triggers and reviewer focus;
- execution-control record when one exists.

Do not infer a missing baseline or ownership snapshot from a moving merge base
or the current status. Return the missing input to the manager. In audit-only
mode, continue only in the context that made the named changes; derive its
touched regions from edit history and the fixed-baseline diff while excluding
the ownership snapshot.

## Phase 1: Implement

Skip this phase in audit-only mode.

1. Confirm the execution card, scope, decisions, and acceptance checks remain
   current. Read the assigned plan items, applicable `AGENTS.md` files, current
   implementation, dependencies, helpers, and mirrored patterns before edits.
2. Implement the smallest complete assigned change. Preserve all snapshotted
   pre-existing work and leave unrelated cleanup alone.
3. If plan and repository reality materially conflict, stop that item. Quote
   the plan assumption and repository evidence as a residual; never improvise
   a replacement design. Continue only independent valid items.
4. Apply compatible specialist instructions in this same context. If a
   specialist requires another role, return the requirement to the manager
   instead of invoking it.
5. Run only focused reads, searches, traces, and static checks needed for
   internal coherence. The manager owns compilation, runtime checks, harness
   work, and all independent reviews.
6. Record each changed file and exact function/type/section. For code, emit an
   affected-site trigger for each signature, identity, semantics, layout,
   guard-affinity, sweep, or mirror concern, including symbol/pattern and search
   scope. Code always returns propagation work to `/update-affected-code`, even
   when the trigger is `none found`; propagation is N/A only when no code changed.
7. For ignored or non-worktree state, follow its owner contract and report the
   exact path, persistence mechanism, expected persistence, and result.

## Phase 2: Same-Context Audit

The worker that made the changes performs this phase before returning. It never
substitutes for domain, adversarial, or session review.

List zero to seven correctness or requirement risks, ranked by severity. Each
item is falsifiable: Claim names the possible failure, Check names a
read/search/trace/static check, and Result states the evidence. Investigate
every inline-checkable item, fix confirmed in-scope problems, and repeat its
check. Hand off anything requiring compilation, runtime behavior, hardware,
user knowledge, or an independent role.

Cover relevant dimensions without manufacturing doubts:

- calling context, threading, lifetime, ranges, phases, reachability,
  determinism, layout, and ownership;
- unread callers/callees/producers/consumers and memory-sourced symbols;
- current-file reread after edits;
- plan/scope attribution against the fixed baseline and ownership snapshot;
- partial sweeps and mirrored client/server or collection wiring;
- smallest plausible crash, corruption, or desync path;
- substantial new logic duplicated from an existing helper.

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
names every consuming target and configuration/platform. The manager runs the
build/runtime and routes results or later fix work.
