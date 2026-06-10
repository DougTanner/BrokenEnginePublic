---
name: external-deep-analysis
description: Runs a two-phase code analysis pipeline on a directory — architecture review (shape) then refactor-clean (in-function mechanics) — then scores, prioritizes, and verifies all plan files. Produces actionable plan files per phase and a tiered `Order.md` priority matrix. Only invoke when the user explicitly requests it (e.g., "/external-deep-analysis", "run a deep analysis", "full code analysis"). Never trigger autonomously from general code questions or during routine code changes.
disable-model-invocation: true
allowed-tools: [Read, Write, Edit, Grep, Glob, Agent, Bash]
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

Derive a plan output directory under `Documents/Plans/`, matching the existing area subfolders there (`Documents/Plans/CLAUDE.md` lists them). For `Engine/` and `Projects/` sources, use the relative portion after `/Source/`; for top-level trees (`Common/`, `DataPacker/`), use the top-level directory name.

- Input: `Engine/Source/Frame` → Output: `Documents/Plans/Frame/`
- Input: `Projects/BrokenEngineSandbox/Source/Frame/Collections` → Output: `Documents/Plans/Frame/Collections/`
- Input: `Common/Threading` → Output: `Documents/Plans/Common/`
- Input: `DataPacker/Source/ExportJobs` → Output: `Documents/Plans/DataPacker/`

If existing plans for the same code already live in a different area folder, match them. Create the output directory if it doesn't exist.

### 1. Phase 1: Architecture Review

Read `.claude/skills/external-architecture-review/SKILL.md` and execute its workflow on the target directory, passing the recursion preference. (Its `disable-model-invocation` flag blocks Skill-tool invocation — follow the file directly.)

When the report completes, split findings into two groups:

- **Actionable items**: Findings with clear fix steps and specific file:line locations. Write each logical group as a plan file: `Documents/Plans/<relative-path>/Architecture_<GroupName>.md`.
- **Investigation items**: Findings that are line-level or need the in-function lens. Collect these as a structured list for Phase 2 handoff:
  ```
  Investigation Items from Phase 1:
  - <file path> — <one-line description of what needs depth>
  ```

#### Writing Plan Files

Follow the plan-authoring rules in `Documents/Plans/CLAUDE.md` (read it before writing the first plan — it owns the required shape and the Order.md bookkeeping). Format:

```
# Architecture: <Group Name>

## Context
Source: /external-architecture-review on <target path>. <Why this group matters.>

## Design

### <File Path>
- <Specific change, naming the symbol/interface plus line numbers> [~Xm]

## Critical files
- <files touched>

## Out of scope
- <adjacent things this plan deliberately does not address>
```

Name the interface being changed, not just `path:line` — symbol names survive line drift, and `/next-plan` relies on them to refresh citations at execution time. Include a rough effort estimate per item: `[~5m]`, `[~15m]`, `[~30m]`, `[~1h]`.

Keep plans focused — one plan per logical group (e.g., `Architecture_IncludeGraph.md`, `Architecture_LayerViolations.md`, `Architecture_CollectionCohesion.md`, `Architecture_LibraryReplacement.md`). Only create a plan if there are concrete changes to make. If a plan would exceed 15 items, split it by subdirectory or file group.

ThirdParty library-replacement candidates from the architecture review get their own plan file (`Architecture_LibraryReplacement.md`). Each item must include: candidate library name, license (allow-list only), approximate LOC removable, and risks. Phase 4 verification re-checks the license claim and confirms the library is not already in `/ThirdParty/`.

### 2. Phase 2: Refactor-Clean

Read `.claude/skills/external-refactor-clean/SKILL.md` and execute its workflow, passing the recursion preference (same as Phase 1 — the flag blocks Skill-tool invocation, so follow the file directly). Target both:
- The original target directory (for full coverage of in-function mechanics)
- Any specific paths flagged as investigation items from Phase 1

**Deduplication**: Before writing plan files, check existing Phase-1 plan files. If a finding is already covered by an existing plan (even under a different category), skip it.

Write all new actionable findings as plan files: `Refactor_<GroupName>.md`, using the same format as Phase 1 with title `# Refactor: <Group Name>` and `Source: /external-refactor-clean on <target path>`.

### 3. Phase 3: Scoring, Tiering & Debt Summary

Score each plan file from Phases 1–2 using the canonical anchors in `Documents/CLAUDE.md` §Scoring Anchors — read that section before scoring:

- **Effort** 1–5, **Impact** 1–5, **Risks** 0–4
- **Priority Score** = Effort − Impact + Risks (lower = higher priority)
- **Tier** — informal size descriptor mirroring the Effort anchor: **Quick Win / Small / Medium / Large / Architectural**

Calibrate against neighbouring rows in the existing `Documents/Plans/Order.md` rather than defaulting to the middle. The Risks axis keys on blast radius, not just likelihood: anything touching determinism / CRC / network protocol / cross-frame state scores 3+ even when the edit is mechanical.

**Debt Score** for the target area as a whole (single label): **LOW / MODERATE / HIGH / CRITICAL**, with one-sentence justification. Use the distribution of tiers as the primary signal (all Quick Wins → LOW; several Architectural → HIGH/CRITICAL).

### 4. Phase 4: Verification Pass

After scoring, launch a verification subagent with `subagent_type: "general-purpose"` and `model: "fable"`. State the expected mutations up front in the prompt: the agent will rewrite plan files (`Read`, `Write`, `Edit`, `Grep`, `Glob`) and delete entirely-invalid ones (`Bash`).

The verification agent reads every plan file created during this run and checks:

1. **Correctness**: Do the file paths and line numbers referenced actually exist? Are the suggested changes consistent with how the codebase actually works? Read the referenced source files to verify.
2. **Benefit**: Would each change actually improve the codebase? Filter out changes that are:
   - Cosmetic-only with no functional benefit
   - Risk-introducing (could break existing behavior)
   - Contradicting engine patterns documented in CLAUDE.md files
   - Duplicating work already covered by another plan file
3. **Completeness**: Are the change descriptions specific enough to act on without ambiguity?
4. **Library-replacement claims** (`Architecture_LibraryReplacement.md` only): re-verify each proposed license against the allow list in `ThirdParty/CLAUDE.md` and confirm the library is not already in `/ThirdParty/`.

For any plan file that fails verification:
- If partially valid: rewrite it with only the valid items
- If entirely invalid: delete it
- Add a `## Verification Notes` section at the bottom of each surviving plan file with any caveats

### 5. Phase 5: Update `Order.md`

After verification, add every surviving plan file to `Documents/Plans/Order.md` — in the same session that created the plan files. `Documents/Plans/CLAUDE.md` owns the row format and bookkeeping rules; all plans across all target paths intermingle in the single score-sorted `## Plans` table:

```markdown
| # | Plan | Tier | Effort | Impact | Risks | Score | Notes |
|---|------|------|--------|--------|-------|-------|-------|
| 1 | [Frame/Architecture_IncludeGraph.md](Frame/Architecture_IncludeGraph.md) | Quick Win | 1 | 2 | 1 | 0 | One-line summary |
```

- Insert each row at its score-correct position (lowest first), then renumber the `#` column so it stays a contiguous 1-based ordinal. Preserve existing rows.
- Add a `## Debt Score (<area>, this run): <LOW / MODERATE / HIGH / CRITICAL>` section above the table with the one-sentence justification.
- New plans that must execute in a fixed order (shared files, stale line numbers) get a bullet in the `## Dependencies` section (`PlanA` → `PlanB` — reason); plans touching the same files get an entry in `## File Groups` (**shared files**: `PlanA`, `PlanB`) so they land in one session.

### 6. Phase 6: Summary

Output a summary listing:
- All plan files created (with paths)
- Number of actionable items per plan
- Debt Score for this run
- Any items removed during verification and why
- The prioritization matrix (so the user sees it inline too)
