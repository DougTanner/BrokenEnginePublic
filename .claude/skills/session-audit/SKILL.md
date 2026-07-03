---
name: session-audit
description: >-
  Final fresh-eyes audit of a logical group of files changed this session (C++
  Code Change Process step 9) — whole-file coherence, cross-file integration,
  and the concrete failure modes earlier steps structurally miss
  (fix-introduced desync, half-applied mirrored edits, doc/code drift from late
  renames, edits no reviewer ever saw). Invoke after all fix/review/build steps
  complete, once per file group. ALSO use when the user asks for a "session
  audit", a final fresh-eyes pass, or to check everything changed this session
  as a whole. Findings only — never edits.
allowed-tools: [Read, Grep, Glob]
---

# Session Audit

Fresh-eyes lens, not a re-run of the step 4–8 checklists: read each assigned file whole, then check the group's cross-file story against the plan's intent. Findings only; the caller dispatches fixes.

## Inputs (from caller)
- File group (paths) + functions/regions touched this session, attributed per step (at minimum: the step 2–3 set vs later-fix regions); if attribution is missing, treat every touched region as potentially post-review
- Accumulated residuals and self-audit focus areas from earlier steps
- The plan document or intent summary

If invoked directly with no caller briefing, reconstruct the file group and touched regions from the conversation history's edits, and treat the residual list as empty.

## Failure-mode checklist

Earlier steps each see a slice; these surface only when reading the finished whole. Check every item explicitly:

1. **Fix-introduced desync** — a step 4/5/8 fix landed *after* the determinism review. Re-check any post-review edit inside CRC'd state (PostRender members, Update logic, RNG draws, serialization) for float-op ordering, RNG draw-count parity, and correct phase placement.
2. **Half-applied mirrored edits (backstop)** — steps 3–4 own the full sweep; focus on mirrors touched by post-step-3 fixes, plus one spot-check of the plan's central mirror. One side updated, counterpart missed: client vs server branch, per-collection pattern applied to N−1 of N collections, C++ struct vs shared GLSL header, Spawn vs Transfer vs AllocateAndCopy vs LogDifferences. Grep the sibling sites; don't trust the diff narrative.
3. **Doc/code drift from late renames** — a step 5 style fix or step 8 compile fix renamed a symbol step 6's docs never saw (steps 5/6 run concurrently; step 8 runs after). Grep this session's changed CLAUDE.md / plan / diagram text for symbols that no longer exist in the code.
4. **Unreviewed late edits** — fixes landed in steps 4–8 (review fixes, style fixes, compile-error fixes) were never themselves reviewed; step 3's propagation edits were (step 4 ran after them). Give diff-of-the-diff attention to everything outside the step 2 + step 3 change set — including any late edit that added or removed a file-wide `BT_CLIENT`/`BT_SERVER` guard (its vcxproj affinity is now stale vs step 7's verification).
5. **Whole-file incoherence** — the file no longer reads as one design: logic duplicated between an old and a new path, a helper the session's edits made dead, a comment or ASSERT contradicting the new behavior, a `#include`/guard the edits made unnecessary.
6. **Residual leakage** — every residual and focus area handed in is either resolved in current code or re-reported; never silently gone.

## Output

Per finding: `path:line`, failure-mode number, one-line description, fix size (**small** — dispatchable now | **structural** — route to a step 10 plan).

Example:
> `Projects/BrokenEngineSandbox/Source/Frame/Blasters.cpp:212` — mode 2 — `Spawn()` initializes the new `mChargeTime` member but `Transfer()` does not copy it, so cross-cell transfer leaves it stale — **small**

If clean, state which checklist items were checked and found clean. End with the standard residuals footer (root CLAUDE.md process rules).
