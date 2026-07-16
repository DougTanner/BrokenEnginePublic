---
name: session-audit
description: >-
  Conditional final fresh-eyes audit of the complete logical change — whole-file
  coherence, cross-file integration, and failure modes earlier checks can miss.
  Invoke after reconciliation only for late semantic fixes, reconciliation
  edits or invalidated assumptions, Tier-3 cross-file integration, or
  contract-significant regions no correctness reviewer saw. ALSO use when the
  user asks for a "session audit" or final fresh-eyes pass. Findings only —
  never edits.
allowed-tools: [Read, Write, Grep, Glob, PowerShell]
---

# Session Audit

Fresh-eyes lens, not a repeat of correctness review or verification: read the
complete logical change after reconciliation, then check its whole-file and
cross-file story against the plan's intent. Findings only; the caller
dispatches fixes.

Run one audit only when at least one trigger is recorded: late semantic fixes;
reconciliation edits or an assumption reconciliation invalidated; Tier-3
cross-file integration; or contract-significant regions unseen by the
correctness review. Otherwise report that the role is not triggered and stop.
A second audit is allowed only when the caller records a distinct,
non-overlapping evidence domain; it must not repeat the same hypotheses or
checklist coverage for model consensus. Any overlapping file must be necessary
to the separate evidence domain.

## Inputs (from caller)
- Complete logical change (all paths), keeping code and documentation lenses distinct within the same audit
- Functions/regions touched this session, attributed as implementation/propagation, review fixes, conditional-role edits, or reconciliation edits; if attribution is missing, treat every touched region as potentially late
- The recorded trigger for this audit and, for an optional second audit, its non-overlapping evidence domain
- Earlier inline residuals and focus areas relevant to the logical change
- The plan document or intent summary

If invoked directly with no caller briefing, reconstruct the complete logical change and touched regions from the conversation history's edits, and treat the residual list as empty.

## Failure-mode checklist

Earlier roles each see a slice; these surface only when reading the finished whole. Check every item explicitly:

1. **Fix-introduced desync** — a semantic fix landed after determinism review. Re-check any post-review edit inside CRC'd state (PostRender members, Update logic, RNG draws, serialization) for float-op ordering, RNG draw-count parity, and correct phase placement.
2. **Half-applied mirrored edits (backstop)** — affected-site propagation and correctness review own the full sweep; focus on mirrors touched by later fixes or reconciliation, plus one spot-check of the plan's central mirror. One side updated, counterpart missed: client vs server branch, per-collection pattern applied to N−1 of N collections, C++ struct vs shared GLSL header, Spawn vs Transfer vs AllocateAndCopy vs LogDifferences. Grep the sibling sites; don't trust the diff narrative.
3. **Doc/code drift from late renames** — a late compile or reconciliation fix renamed a symbol after conditional documentation updates. Grep this session's changed AGENTS.md / plan / diagram text for symbols that no longer exist in the code. If this session created a new directory `AGENTS.md` or `CLAUDE.md`, verify the pair: the `AGENTS.md` has a sibling `CLAUDE.md` containing exactly `@AGENTS.md`, and no `CLAUDE.md` stub was left without its `AGENTS.md`. (No extra scan when the session created neither.)
4. **Unreviewed late edits** — semantic fixes, conditional-role edits, or reconciliation edits were not seen by the correctness review. Give diff-of-the-diff attention to those regions, including any late edit that added or removed a file-wide `BT_CLIENT`/`BT_SERVER` guard after project-membership verification.
5. **Whole-file incoherence** — the file no longer reads as one design: logic duplicated between an old and a new path, a helper the session's edits made dead, a comment or ASSERT contradicting the new behavior, a `#include`/guard the edits made unnecessary.
6. **Residual leakage** — every residual and focus area handed in is either resolved in current code or re-reported; never silently gone.
7. **False completion** — earlier reports are claims, not evidence: for each accepted fix and each residual marked resolved, spot-check the change actually exists in current code.
8. **Debris** — diagnostic `LOG`s left at kDebug/kVerbose from iteration, commented-out code, scratch/temp files introduced this session. Global `%LOCALAPPDATA%\BrokenEngine\AgentReports\` artifacts conforming to the shared reporting contract are intentional process state, not debris. Changelog-style comments belong to repo-code-review §2d — don't double-report.

## Output

Return the complete output inline. The caller must read every finding before
deduplication and classification.

Per finding: `path:line`, failure-mode number, one-line description, fix size
(**small** — dispatchable now | **structural** — blocks when it is an in-scope
acceptance failure; only proven pre-existing or out-of-scope work routes to a
follow-up plan).

Example:
> `Projects/BrokenEngineSandbox/Source/Frame/Blasters.cpp:212` — mode 2 — `Spawn()` initializes the new `mChargeTime` member but `Transfer()` does not copy it, so cross-cell transfer leaves it stale — **small**

If clean, state which checklist items were checked and found clean. Append this final footer in every case:

```text
Files changed: none
Functions/regions touched: none
Residuals:
- <pre-existing issue or incomplete audit item, or none>
```
