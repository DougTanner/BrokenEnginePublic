---
name: external-self-audit
description: >-
  Post-implementation self-audit run by the implementing context itself — lists
  what it is least confident about in the changes just made (capped,
  evidence-required), investigates each item to root cause, fixes confirmed
  problems, and hands uncheckable items to downstream reviewers as focus areas.
  Runs immediately after implementation, before any fresh-context reviews. Also
  use when the user asks "what are you least confident about", "audit your
  assumptions", or "what did you not verify". Must run in whatever context made
  the changes (an implementation subagent, or the main session for direct
  edits), never a fresh fork — a fresh context cannot see the implementer's
  assumptions.
allowed-tools: [Read, Grep, Glob, Edit, "Bash(git diff *)", "Bash(git status *)", "Bash(bash *msbuild.sh*)"]
---

# Self-Audit

Audit your own epistemic state after implementing: what you assumed without verifying, what you never read, where you silently deviated from the plan. The fresh-context reviewers downstream see only the diff — they cannot see the reasoning that produced it. This step externalizes that reasoning: resolve what you can now, hand the rest to those reviewers as focus areas.

## Rules

- List at most 3–7 items total across all rubric dimensions, ranked by severity. Zero findings on a dimension is a valid answer — never invent one. Open-ended "list your doubts" prompting measurably produces confabulated hedges (unverified claims dominate false review findings, and elaborate prompting makes it worse); the cap and the evidence requirement exist to prevent that.
- Only flag items affecting correctness or the stated requirements. Style, polish, and hypothetical-future concerns belong to the downstream reviews.
- Every item must be falsifiable — three parts: **claim** ("I assumed X"), **check** (what to read/grep/trace, and what result confirms or refutes the claim), **result** (what the check showed). "I'm not sure about X" with no check is a hedge — sharpen it or drop it.
- No confidence percentages — verbalized confidence is miscalibrated. For a shaky item, ask instead: what are the 2–3 concrete ways this could be wrong? Each way becomes a check.
- Investigate every checkable item in-session to root cause — do the Grep/Read legwork inline (this skill runs inside the implementing context, which does not spawn subagents). Fix confirmed problems immediately via the normal edit flow, then re-run the item's check.
- Only genuinely uncheckable-in-session items (needs runtime behavior, user knowledge, hardware) survive as handoff notes.
- If any fix edited code, rebuild the affected `.cpp` files before the final report and fix compile errors the fixes introduced — the implementation build precedes this audit, so an unbuilt fix would otherwise break only at the process's final build, in a context blind to the audit's reasoning. Subagents build inline by running the `/compile` skill's selective `--files` `msbuild.sh` command directly via Bash (the syntax is already in context from the implementation build); the main session dispatches the build per the model table.

## Rubric

Walk each dimension; skip ones irrelevant to the change:

1. **Unverified assumptions** — calling context, threading/`Dispatch()` safety, lifetime/ownership, value ranges, frame phase (Update vs PostRender), client/server build reachability, determinism — which did the implementation take on faith?
2. **Unread dependencies** — files/functions the change depends on (callers, callees, data producers/consumers) never opened this session. Read them now.
3. **Stale in-session understanding** — files read early and edited since; memory of them may not match their current state. Re-read the touched regions.
4. **Memory-sourced symbols** — any API, member, constant, or pattern written from training memory rather than verified against this codebase. Grep/Read to confirm existence and signature.
5. **Plan divergence & scope drift** — compare the changed-file/function list against the plan document. Use the top-level session's fixed session-start commit as the changed-file baseline; all implementation subagents share that worktree and baseline. Do not replace it with a moving merge-base after primary-branch reconciliation. Cross-check against your Edit/Write history so pre-existing or newly integrated changes are not misattributed to your implementation. Every divergence must already be announced or surfaced now; flag files outside the plan's scope.
6. **Partial sweeps** — for any repeated pattern change (rename, signature change, new collection member wired through AllocateAndCopy/LogDifferences/Spawn/Transfer), confirm exhaustiveness by Grep, not recall.
7. **Premortem** — what is the smallest thing that could still be wrong and cause a crash or desync in-game? One targeted answer, then check it.
8. **Pasted logic** — did any new block duplicate existing repo code instead of calling/extracting a shared helper? Grep a distinctive line from each substantial new block. Deliberate mirrored patterns (client/server pairs, per-collection boilerplate) are exempt.

## Report and Handoff

End with a short report:

- **Resolved**: each item as claim → check → result (and the fix, if one was made)
- **Handed off**: items uncheckable in-session, each phrased as a specific verification request — "verify X holds when Y", never "double-check the audio code"

When running inside an implementation subagent, return the handed-off items verbatim in the final report so the caller can pass them to downstream reviewers as focus areas. When running in the main session, include them in the reviewer prompts directly. Specific requests keep reviewers grounded; vague worries seed their own over-correction.
