---
name: external-deep-analysis
description: Runs a full three-phase code analysis pipeline on a directory — tech-debt scan, architecture review, and refactoring analysis — in that order. Each phase produces actionable plan files, and items needing further investigation feed into the next phase. A final verification pass ensures all plans are correct and beneficial. Only invoke when the user explicitly requests it (e.g., "/external-deep-analysis", "run a deep analysis", "full code analysis"). Never trigger autonomously from general code questions or during routine code changes.
allowed-tools: [Read, Write, Grep, Glob, Task, Agent, Bash, Skill]
---

# Deep Analysis Pipeline

Runs three analysis skills in sequence (broad → specific), producing actionable plan files at each stage and feeding items that need further investigation into the next phase.

**Order** (from CLAUDE.md):
1. `/external-tech-debt` — Broad scan, prioritizes debt across 8 categories
2. `/external-architecture-review` — Analyzes dependencies, pattern compliance, coupling
3. `/external-refactor-clean` — Actionable refactoring recommendations for specific files

## Arguments

The user provides a target path (file or directory) to analyze. If no path is given, ask for one.

**Recursion**: By default, only analyze code files directly in the specified directory (non-recursive). Only recurse into sub-directories if the user explicitly requests it (e.g., "recurse", "recursive", "include subdirectories"). Pass this recursion preference to each sub-skill invocation by appending the instruction to the skill arguments (e.g., `/external-tech-debt Engine/Source/Frame — non-recursive, only files directly in this directory`).

## Instructions

### 0. Determine Output Path

Derive a plan output directory from the target path by extracting the relative portion after `/Source/`. If the path contains no `/Source/`, use the directory name itself (e.g., top-level directories like `Common/`).

- Input: `Engine/Source/Frame` → Output: `Documents/Plans/Frame/`
- Input: `Projects/BrokenEngineSandbox/Source/Frame/Collections` → Output: `Documents/Plans/Frame/Collections/`
- Input: `Common/Source/Threading` → Output: `Documents/Plans/Threading/`
- Input: `Common/` → Output: `Documents/Plans/Common/`

Create the output directory if it doesn't exist.

### 1. Phase 1: Tech Debt Scan

Invoke the `/external-tech-debt` skill on the target directory, passing the recursion preference.

When the report completes, analyze its findings and split them into two groups:

- **Actionable items**: Findings with clear fix steps (dead code removal, unused includes, quick wins, medium-effort items with specific file:line locations). Write each group as a plan file.
- **Investigation items**: Findings that are architectural, vague, or need deeper analysis (collection restructuring, manager decoupling, systemic issues). Collect these into a structured list for Phase 2 handoff:
  ```
  Investigation Items from Phase 1:
  - <file path> — <one-line description of what needs deeper analysis>
  - <file path> — <one-line description>
  ```

#### Writing Plan Files

For each actionable group, write a plan file to the output directory:

```
Documents/Plans/<relative-path>/TechDebt_<GroupName>.md
```

Plan file format (matches what plan mode produces):

```
# Tech Debt: <Group Name>

Source: /external-tech-debt on <target path>

## Changes

### <File Path>
- <Specific change to make with line numbers> [~Xm]

### <File Path>
- <Specific change to make with line numbers> [~Xm]
```

Include a rough effort estimate per item: `[~5m]`, `[~15m]`, `[~30m]`, `[~1h]`. This helps with session planning.

Keep plans focused — one plan per logical group of changes (e.g., `TechDebt_DeadCode.md`, `TechDebt_UnusedIncludes.md`, `TechDebt_DuplicationFixes.md`). Only create a plan if there are concrete changes to make. If a plan would exceed 15 items, split it by subdirectory or file group (e.g., `TechDebt_DeadCode_Frame.md`, `TechDebt_DeadCode_Audio.md`).

### 2. Phase 2: Architecture Review

Invoke the `/external-architecture-review` skill, passing the recursion preference. Target both:
- The original target directory (for full coverage)
- Any specific paths flagged as investigation items from Phase 1

When the report completes, split findings the same way:

- **Actionable items** → Write plan files as `Architecture_<GroupName>.md`
- **Investigation items** → Collect paths and descriptions into a structured list (file path + one-line description per item) for the Phase 3 handoff

Plan file format:

```
# Architecture: <Group Name>

Source: /external-architecture-review on <target path>

## Changes

### <File Path>
- <Specific change to make>
```

### 3. Phase 3: Refactoring Analysis

**Skip this phase entirely if Phase 2 produced no investigation items.** Only run Phase 3 when there are specific items that Phases 1-2 flagged but could not fully resolve.

Invoke the `/external-refactor-clean` skill, passing the recursion preference, on the specific files/paths flagged as investigation items from Phase 2.

**Deduplication**: Before writing plan files, check all existing plan files from Phases 1-2. If a finding is already covered by an existing plan (even under a different category), skip it. Do not re-categorize items that are already planned.

Write all new actionable findings as plan files: `Refactor_<GroupName>.md`

Plan file format:

```
# Refactor: <Group Name>

Source: /external-refactor-clean on <target path>

## Changes

### <File Path>
- <Specific change to make with line numbers>
```

### 4. Verification Pass

After all three phases complete and plan files are written, launch an Opus subagent to perform a final verification:

The verification agent should read every plan file created during this run and check:

1. **Correctness**: Do the file paths and line numbers referenced actually exist? Are the suggested changes consistent with how the codebase actually works? Read the referenced source files to verify.
2. **Benefit**: Would each change actually improve the codebase? Filter out changes that are:
   - Cosmetic-only with no functional benefit
   - Risk-introducing (could break existing behavior)
   - Contradicting engine patterns documented in CLAUDE.md files
   - Duplicating work already covered by another plan file
3. **Completeness**: Are the change descriptions specific enough to act on without ambiguity?

For any plan file that fails verification:
- If partially valid: rewrite it with only the valid items
- If entirely invalid: delete it
- Add a `## Verification Notes` section at the bottom of each surviving plan file with any caveats

### 5. Prioritization

After verification, create or update `Documents/Plans/Order.md`. All plans across all target paths are intermingled in a single table sorted by priority score.

Rate each plan on three axes (1-10 scale):
- **Effort** (1=trivial deletion, 10=massive cross-file rewrite)
- **Impact** (1=cosmetic, 10=fixes critical bugs or prevents future breakage)
- **Risks** (1=safe pure deletion, 10=high chance of introducing new bugs)

**Priority Score** = Effort - Impact + Risks (lower = higher priority). Sort the table ascending by score.

**Rating guidance:**
- Pure dead code removal: Effort 1-2, Impact 2-3, Risks 1
- Thread-safety / correctness fixes: Effort 1-3, Impact 6-8, Risks 1-2
- Deprecated API replacement: Effort 3-4, Impact 5-7, Risks 3-5
- Mechanical refactors (dedup, extract helper): Effort 2-4, Impact 3-5, Risks 2
- Cross-file moves / file splits: Effort 4-6, Impact 3-5, Risks 2-4
- Architectural restructuring: Effort 5-7, Impact 3-5, Risks 3-5

```markdown
# Plan Execution Order

Score = Effort - Impact + Risks (lower = higher priority)

| # | Plan | Effort | Impact | Risks | Score | Items | Notes |
|---|------|--------|--------|-------|-------|-------|-------|
| 1 | `Path/PlanName.md` | X | X | X | X | N | One-line summary |
```

After the table, add two sections:

```markdown
## Dependencies

Plans that must be executed in order due to shared files or stale line numbers:

- `PlanA` → `PlanB` — reason

## File Groups

Plans that touch the same files and should be done in a single session:

- **shared files**: `PlanA`, `PlanB`, ...
```

If `Order.md` already exists from a previous run, merge new entries into the existing table (add new rows, update changed rows, preserve unchanged rows). Re-sort the entire table by score after merging.

### 6. Summary

After the prioritization matrix is written, output a summary listing:
- All plan files created (with paths)
- Number of actionable items per plan
- Any items that were removed during verification and why
- The prioritization matrix (so the user sees it inline too)
