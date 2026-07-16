---
name: external-deep-analysis
description: Runs a two-phase code analysis pipeline on a directory — architecture review (shape) then refactor-clean (in-function mechanics) — then verifies, scores, and prioritizes all plan files. Produces actionable plan files per phase and registers them through WorktreeCli's deterministic plan queue. Both phases also surface the non-security anti-patterns characteristic of iteratively AI-generated code (dead modules, broken abstractions, phantom guards, swallowed errors, cross-file duplication); security auditing is deliberately excluded. Only invoke when the user explicitly requests it (e.g., "/external-deep-analysis", "run a deep analysis", "full code analysis"). Never trigger autonomously from general code questions or during routine code changes.
disable-model-invocation: true
allowed-tools: [Read, Write, Edit, Grep, Glob, Agent, Bash]
---

# Deep Analysis Pipeline

Runs two analysis skills in sequence (shape → in-function), then verifies and scores all output.

**Pipeline order:**
1. `/external-architecture-review` — Shape: dependency structure, deep-modules (Ousterhout), coupling/cohesion, determinism, frame-phase, thread-model, shader/CPU consistency, ThirdParty library-replacement opportunities, plus AI-generation structural anti-patterns (dead modules, cosmetic/broken abstractions, pattern abandonment, cross-file duplication, inter-module seams)
2. `/external-refactor-clean` — In-function mechanics: complexity, hot-path allocation, bool-proliferation, `Float4A`, narrow `#ifdef`, header placement, plus AI-generation in-function smells (phantom guards, swallowed errors, return-type/boundary gaps). Hands off oversized files to `/reduce-file`.

## Arguments

The user provides a target path (file or directory) to analyze. If no path is given, ask for one.

**Recursion**: By default, only analyze code files directly in the specified directory (non-recursive). Only recurse into sub-directories if the user explicitly requests it (e.g., "recurse", "recursive", "include subdirectories"). The sub-skills share the same non-recursive default; still **state the recursion mode explicitly in every sub-skill invocation, in both modes**, so the user's choice propagates (e.g., `/external-architecture-review Engine/Source/Frame — non-recursive, only files directly in this directory`, or `… — recursive, include subdirectories`).

## Instructions

### 0. Determine Output Path

Derive a plan output directory under `Documents/Plans/`, matching the existing area subfolders there (`Documents/Plans/AGENTS.md` lists them). For `Engine/` and `Projects/` sources, use the relative portion after `/Source/`; for top-level trees (`Common/`, `DataPacker/`), use the top-level directory name.

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

Follow the plan-authoring rules in `Documents/Plans/AGENTS.md` (read it before writing the first plan — it owns the required shape, structured dependencies, Coordination policy, and WorktreeCli queue-submission contract). Format:

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

## Notes
- Invariant exposure: <does this touch determinism/CRC sim paths, `kiVersion`/`.pack` layout, replays, client/server guard scope, or allocation-tracked paths? State "none" if none>
- <pre-stage any single open decision for /external-grill-plan>
```

Name the interface being changed, not just `path:line` — symbol names survive line drift, and `/next-plan` relies on them to refresh citations at execution time. Include a rough effort estimate per item: `[~5m]`, `[~15m]`, `[~30m]`, `[~1h]`.

Keep plans focused — one plan per logical group (e.g., `Architecture_IncludeGraph.md`, `Architecture_LayerViolations.md`, `Architecture_CollectionCohesion.md`, `Architecture_LibraryReplacement.md`). Only create a plan if there are concrete changes to make. If a plan would exceed 15 items, split it by subdirectory or file group.

ThirdParty library-replacement candidates from the architecture review get their own plan file (`Architecture_LibraryReplacement.md`). Each item must include: candidate library name, license (allow-list only), approximate `bt-token-v1` removable measured with `.agents/scripts/Measure-Tokens.ps1`, and risks. The verification phase re-checks the license claim and confirms the library is not already in `/ThirdParty/`.

### 2. Phase 2: Refactor-Clean

Read `.claude/skills/external-refactor-clean/SKILL.md` and execute its workflow, passing the recursion preference (same as Phase 1 — the flag blocks Skill-tool invocation, so follow the file directly). Target both:
- The original target directory (for full coverage of in-function mechanics)
- Any specific paths flagged as investigation items from Phase 1

**Deduplication**: Before writing plan files, check the Phase-1 plan files from this run AND pre-existing live plans. Run `plan order validate --repo <common-dir> --worktree <session-worktree>` and use its executable-row inventory together with the output directory; never parse `Documents/Plans/Order.md`. If a finding is already covered by any live plan (even under a different category), skip it.

Write all new actionable findings as plan files: `Refactor_<GroupName>.md`, using the same format as Phase 1 with title `# Refactor: <Group Name>` and `Source: /external-refactor-clean on <target path>`.

### 3. Phase 3: Verification Pass

After Phase 2 completes, launch a verification subagent with `subagent_type: "general-purpose"` and `model: "fable"`. State the expected mutations up front in the prompt: the agent will rewrite plan files (`Read`, `Write`, `Edit`, `Grep`, `Glob`) and delete entirely-invalid ones (`Bash`). Verification runs BEFORE scoring so scores reflect what survives, not what was originally drafted.

The verification agent reads every plan file created during this run and checks:

1. **Correctness**: Do the file paths and line numbers referenced actually exist? Are the suggested changes consistent with how the codebase actually works? Read the referenced source files to verify.
2. **Benefit**: Would each change actually improve the codebase? Filter out changes that are:
   - Cosmetic-only with no functional benefit
   - Risk-introducing (could break existing behavior)
   - Contradicting engine patterns documented in AGENTS.md files
   - Duplicating work already covered by another plan file — including pre-existing live plans from earlier runs
3. **Completeness**: Are the change descriptions specific enough to act on without ambiguity?
4. **Library-replacement claims** (`Architecture_LibraryReplacement.md` only): re-verify each proposed license against the allow list in `ThirdParty/AGENTS.md` and confirm the library is not already in `/ThirdParty/`.

For any plan file that fails verification:
- If partially valid: rewrite it with only the valid items
- If entirely invalid: delete it
- Where a surviving plan has caveats, add a `## Verification Notes` section at the bottom recording them (omit the section when there are none)

### 4. Phase 4: Scoring, Tiering & Debt Summary

Score each surviving plan file (post-verification content) using the canonical anchors in `Documents/AGENTS.md` §Scoring Anchors — read that section before scoring:

- **Effort** 1–5, **Impact** 1–5, **Risks** 0–4
- **Priority Score** = Effort − Impact + Risks (lower = higher priority)
- **Tier** — informal size descriptor mirroring the Effort anchor: **Quick Win / Small / Medium / Large / Architectural**

Calibrate against neighbouring rows from the successful WorktreeCli validation inventory rather than defaulting to the middle. The Risks axis keys on blast radius, not just likelihood: anything touching determinism / CRC / network protocol / cross-frame state scores 3+ even when the edit is mechanical.

**Debt Score** for the target area as a whole (single label), with one-sentence justification. Use the tier distribution as the primary signal:

- **LOW** — all or nearly all Quick Win / Small
- **MODERATE** — mostly Small / Medium
- **HIGH** — multiple Large, or any Architectural
- **CRITICAL** — several Architectural, or any finding threatening a core invariant (determinism / CRC, network protocol, save/pack compatibility)

The Debt Score is reported in the Phase 6 summary only — never included in an WorktreeCli queue request (per `Documents/Plans/AGENTS.md`, run retrospectives don't belong in the priority index).

### 5. Phase 5: Register plans through WorktreeCli

After scoring, write every surviving plan file first, then create one schema-version `1` JSON request beneath the session worktree's `Temp/`:

```json
{
  "schemaVersion": 1,
  "operation": "add",
  "sequences": [[{
    "queue": "plans",
    "plan": "Documents/Plans/Frame/Architecture_IncludeGraph.md",
    "tier": "Quick Win",
    "effort": 1,
    "impact": 2,
    "risks": 1,
    "notes": "One-line summary",
    "dependsOn": []
  }]]
}
```

- Put prerequisite-first plans in the same sequence; WorktreeCli adds each immediate-predecessor edge. Put independent plans in separate sequences and name already-live prerequisites in `dependsOn`.
- Invoke `plan order add --repo <common-dir> --worktree <session-worktree> --owner <token> --session <label> --request <Temp repo-relative JSON>`. Require a receipt covering every intended plan and successful queue unlocks, then require `plan order validate --repo <common-dir> --worktree <session-worktree>` to report `ok: true`.
- Never parse or edit either `Order.md`. A failed add leaves retryable plan-file orphans and no new executable rows.
- Directional prerequisites exist only in `dependsOn`. Mandatory nondirectional constraints require reciprocal `## Coordination` sections in every affected live plan; if existing counterparts need updates, route the set through the atomic multi-plan add/update workflow. Ordinary overlap may remain a nonblocking one-sided warning in plan prose.
- Do **not** include the Debt Score or any other run retrospective in the request; it lives in the Phase 6 summary only.

### 6. Phase 6: Summary

Output a summary listing:
- All plan files created (with paths)
- Number of actionable items per plan
- Debt Score for this run
- Any items removed during verification and why
- The prioritization matrix (so the user sees it inline too)
