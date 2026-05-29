---
name: external-deep-analysis
description: Runs a two-phase code analysis pipeline on a directory — architecture review (shape) then refactor-clean (in-function mechanics) — then scores, prioritizes, and verifies all plan files. Produces actionable plan files per phase and a tiered `Order.md` priority matrix. Only invoke when the user explicitly requests it (e.g., "/external-deep-analysis", "run a deep analysis", "full code analysis"). Never trigger autonomously from general code questions or during routine code changes.
disable-model-invocation: true
allowed-tools: [Read, Write, Edit, Grep, Glob, Agent, Bash, Skill]
---

# Deep Analysis Pipeline

Runs two analysis skills in sequence (shape → in-function), then scores and verifies all output.

**Pipeline order:**
1. `/external-architecture-review` — Shape: dependency structure, deep-modules (Ousterhout), coupling/cohesion, determinism, frame-phase, thread-model, shader/CPU consistency, ThirdParty library-replacement opportunities
2. `/external-refactor-clean` — In-function mechanics: complexity, hot-path allocation, bool-proliferation, `Float4A`, narrow `#ifdef`, header placement. Hands off oversized files to `/reduce-file`.

## Arguments

The user provides a target path (file or directory) to analyze. If no path is given, ask for one.

**Recursion**: By default, only analyze code files directly in the specified directory (non-recursive). Only recurse into sub-directories if the user explicitly requests it (e.g., "recurse", "recursive", "include subdirectories"). Pass this recursion preference to each sub-skill invocation by appending the instruction to the skill arguments (e.g., `/external-architecture-review Engine/Source/Frame — non-recursive, only files directly in this directory`).

## Instructions

### 0. Determine Output Path

Derive a plan output directory from the target path by extracting the relative portion after `/Source/`. If the path contains no `/Source/`, use the directory name itself (e.g., top-level directories like `Common/`).

- Input: `Engine/Source/Frame` → Output: `Documents/Plans/Frame/`
- Input: `Projects/BrokenEngineSandbox/Source/Frame/Collections` → Output: `Documents/Plans/Frame/Collections/`
- Input: `Common/Source/Threading` → Output: `Documents/Plans/Threading/`
- Input: `Common/` → Output: `Documents/Plans/Common/`

Create the output directory if it doesn't exist.

### 1. Phase 1: Architecture Review

Invoke the `/external-architecture-review` skill on the target directory, passing the recursion preference.

When the report completes, split findings into two groups:

- **Actionable items**: Findings with clear fix steps and specific file:line locations. Write each logical group as a plan file: `Documents/Plans/<relative-path>/Architecture_<GroupName>.md`.
- **Investigation items**: Findings that are line-level or need the in-function lens. Collect these as a structured list for Phase 2 handoff:
  ```
  Investigation Items from Phase 1:
  - <file path> — <one-line description of what needs depth>
  ```

#### Writing Plan Files

Plan file format (matches what plan mode produces):

```
# Architecture: <Group Name>

Source: /external-architecture-review on <target path>

## Changes

### <File Path>
- <Specific change to make with line numbers> [~Xm]
```

Include a rough effort estimate per item: `[~5m]`, `[~15m]`, `[~30m]`, `[~1h]`. This helps with session planning.

Keep plans focused — one plan per logical group (e.g., `Architecture_IncludeGraph.md`, `Architecture_LayerViolations.md`, `Architecture_CollectionCohesion.md`, `Architecture_LibraryReplacement.md`). Only create a plan if there are concrete changes to make. If a plan would exceed 15 items, split it by subdirectory or file group.

ThirdParty library-replacement candidates from the architecture review get their own plan file (`Architecture_LibraryReplacement.md`). Each item must include: candidate library name, license (allow-list only), approximate LOC removable, and risks. Phase 4 verification re-checks the license claim and confirms the library is not already in `/ThirdParty/`.

### 2. Phase 2: Refactor-Clean

Invoke the `/external-refactor-clean` skill, passing the recursion preference. Target both:
- The original target directory (for full coverage of in-function mechanics)
- Any specific paths flagged as investigation items from Phase 1

**Deduplication**: Before writing plan files, check existing Phase-1 plan files. If a finding is already covered by an existing plan (even under a different category), skip it.

Write all new actionable findings as plan files: `Refactor_<GroupName>.md`.

Plan file format:

```
# Refactor: <Group Name>

Source: /external-refactor-clean on <target path>

## Changes

### <File Path>
- <Specific change to make with line numbers> [~Xm]
```

### 3. Phase 3: Scoring, Tiering & Debt Summary

For each plan file produced in Phases 1–2, assign:

**Tier** (inherited from the former `external-tech-debt` rubric):
- **Quick Win** (< 15 min each) — Dead code removal, unused include cleanup, simple pattern fixes
- **Medium Effort** (15 min – 2 hours) — Extract duplicated code, split oversized functions, fix layer violations
- **Architectural** (> 2 hours) — Collection restructuring, manager decoupling, major refactors

**Axes** (1–10 scale):
- **Effort** (1=trivial deletion, 10=massive cross-file rewrite)
- **Impact** (1=cosmetic, 10=fixes critical bugs or prevents future breakage)
- **Risks** (1=safe pure deletion, 10=high chance of introducing new bugs)

**Priority Score** = Effort - Impact + Risks (lower = higher priority).

**Rating guidance:**
- Pure dead code removal: Effort 1-2, Impact 2-3, Risks 1
- Thread-safety / correctness fixes: Effort 1-3, Impact 6-8, Risks 1-2
- Deprecated API replacement: Effort 3-4, Impact 5-7, Risks 3-5
- Mechanical refactors (dedup, extract helper): Effort 2-4, Impact 3-5, Risks 2
- Cross-file moves / file splits: Effort 4-6, Impact 3-5, Risks 2-4
- Architectural restructuring: Effort 5-7, Impact 3-5, Risks 3-5

**Debt Score** for the target area as a whole (single label): **LOW / MODERATE / HIGH / CRITICAL**, with one-sentence justification. Use the distribution of tiers as the primary signal (all Quick Wins → LOW; several Architectural → HIGH/CRITICAL).

### 4. Phase 4: Verification Pass

After scoring, launch a verification subagent with `subagent_type: "general-purpose"` and `model: "opus"`. The subagent prompt must explicitly grant `Read`, `Write`, `Edit`, `Grep`, `Glob` so it can delete or rewrite plan files — this skill's `allowed-tools` already includes `Write`/`Edit` so the subagent inherits them, but state the expected mutations up front in the prompt.

The verification agent reads every plan file created during this run and checks:

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

### 5. Phase 5: Write `Order.md`

After verification, create or update `Documents/Plans/Order.md`. All plans across all target paths are intermingled in a single table sorted by priority score.

```markdown
# Plan Execution Order

Score = Effort - Impact + Risks (lower = higher priority)

## Debt Score (this run): [LOW / MODERATE / HIGH / CRITICAL]
[One-sentence justification]

| # | Plan | Tier | Effort | Impact | Risks | Score | Items | Notes |
|---|------|------|--------|--------|-------|-------|-------|-------|
| 1 | `Path/PlanName.md` | Quick Win | X | X | X | X | N | One-line summary |
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

### 6. Phase 6: Summary

Output a summary listing:
- All plan files created (with paths)
- Number of actionable items per plan
- Debt Score for this run
- Any items removed during verification and why
- The prioritization matrix (so the user sees it inline too)
