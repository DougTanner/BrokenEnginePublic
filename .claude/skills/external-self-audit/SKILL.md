---
name: external-self-audit
description: >-
  Post-implementation self-audit run by the implementing session itself — lists
  what it is least confident about in the changes just made (capped,
  evidence-required), investigates each item to root cause, fixes confirmed
  problems, and hands uncheckable items to downstream reviewers as focus areas.
  Step 2b of the CLAUDE.md C++ Code Change Process — invoke immediately after
  implementation (step 2), before any subagent reviews. Also use when the user
  asks "what are you least confident about", "audit your assumptions", or "what
  did you not verify". Must run in the main session, never a fork — a fresh
  subagent cannot see the implementer's assumptions.
allowed-tools: [Read, Grep, Glob, Edit, Agent, "Bash(git diff *)", "Bash(git status *)"]
---

# Self-Audit

Audit your own epistemic state after implementing: what you assumed without verifying, what you never read, where you silently deviated from the plan. The fresh-context reviewers downstream (steps 4, 5, 9) see only the diff — they cannot see the reasoning that produced it. This step externalizes that reasoning: resolve what you can now, hand the rest to those reviewers as focus areas.

## Rules

- List at most 3–7 items total across all rubric dimensions, ranked by severity. Zero findings on a dimension is a valid answer — never invent one. Open-ended "list your doubts" prompting measurably produces confabulated hedges (unverified claims dominate false review findings, and elaborate prompting makes it worse); the cap and the evidence requirement exist to prevent that.
- Only flag items affecting correctness or the stated requirements. Style, polish, and hypothetical-future concerns belong to the downstream reviews.
- Every item must be falsifiable — three parts: **claim** ("I assumed X"), **check** (what to read/grep/trace, and what result confirms or refutes the claim), **result** (what the check showed). "I'm not sure about X" with no check is a hedge — sharpen it or drop it.
- No confidence percentages — verbalized confidence is miscalibrated. For a shaky item, ask instead: what are the 2–3 concrete ways this could be wrong? Each way becomes a check.
- Investigate every checkable item in-session to root cause — spawn Sonnet subagents for search legwork. Fix confirmed problems immediately via the normal edit flow, then re-run the item's check.
- Only genuinely uncheckable-in-session items (needs runtime behavior, user knowledge, hardware) survive as handoff notes.

## Rubric

Walk each dimension; skip ones irrelevant to the change:

1. **Unverified assumptions** — calling context, threading/`Dispatch()` safety, lifetime/ownership, value ranges, frame phase (Update vs PostRender), client/server build reachability, determinism. The same engine concerns `/external-grill-plan` probes before implementation, applied post-hoc: which did the implementation take on faith?
2. **Unread dependencies** — files/functions the change depends on (callers, callees, data producers/consumers) never opened this session. Read them now.
3. **Stale in-session understanding** — files read early and edited since; memory of them may not match their current state. Re-read the touched regions.
4. **Memory-sourced symbols** — any API, member, constant, or pattern written from training memory rather than verified against this codebase. Grep/Read to confirm existence and signature.
5. **Plan divergence & scope drift** — compare the changed-file/function list (read-only git commands are allowed) against the plan document. Every divergence must already be announced or surfaced now; flag files outside the plan's scope.
6. **Partial sweeps** — for any repeated pattern change (rename, signature change, new collection member wired through AllocateAndCopy/LogDifferences/Spawn/Transfer), confirm exhaustiveness by Grep, not recall.
7. **Premortem** — what is the smallest thing that could still be wrong and cause a crash or desync in-game? One targeted answer, then check it.

## Report and Handoff

End with a short report:

- **Resolved**: each item as claim → check → result (and the fix, if one was made)
- **Handed off**: items uncheckable in-session, each phrased as a specific verification request — "verify X holds when Y", never "double-check the audio code"

Include the handed-off items verbatim as focus areas in the step 4 (repo-code-review) and step 9 (final audit) subagent prompts. Specific requests keep those reviewers grounded; vague worries seed their own over-correction.
