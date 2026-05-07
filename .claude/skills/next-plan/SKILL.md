---
name: next-plan
description: Pull the highest-priority plan from `Documents/Plans/Order.md`, follow any unfinished prerequisites, validate it against the current codebase, refresh stale details, and present a ready-to-execute plan for approval. Removes the Order.md row immediately on selection and auto-deletes the source plan file once the final actionable plan has been created. Use when the user invokes `/next-plan`.
disable-model-invocation: true
user-invocable: true
argument-hint: "[plan-file-path]"
allowed-tools: [Read, Grep, Glob, Agent, Edit, Bash, AskUserQuestion]
---

# Next Plan

Walks the `## Plans` table in `Documents/Plans/Order.md`, picks the top-priority unblocked plan, verifies it still describes a real problem in the current code, refreshes stale line numbers or paths, and presents a ready-to-execute plan for explicit user approval.

## Preconditions

- The skill assumes **bypass-permissions** mode and performs two mutations directly: it removes the target row from `Documents/Plans/Order.md` as soon as the candidate is selected, and it deletes the source plan file from disk once the final actionable plan has been generated. Both happen without further confirmation. User approval is requested only on the final synthesized plan, not on the mutations.
- `Documents/Plans/Order.md` must exist. If it does not, report the missing file and stop.
- The skill does **not** require — and does not use — plan mode. It mimics plan mode's "review-before-implement" UX by presenting the final plan and asking for approval via `AskUserQuestion` before the standard C++ Code Change Process begins.

## Order.md structure reference

Order.md has a single `## Plans` table at roughly line 21. Columns are: `# | Plan | Tier | Effort | Impact | Risks | Score | Notes`. Rows are sorted by Score ascending (lowest = highest priority). Every row in the table is executable; rows are deleted from the table when the plan is done. Plan cells should be markdown links (`[path](path)`) for clickable navigation.

- **`### Reference / Index Documents` subsection**: a separate table below the main one, listing meta/overview docs that are never executed as plans. **Ignore this subsection entirely.**
- **`## Dependencies` section**: prose bullets expressing ordering constraints between plans.

## Workflow

Execute these steps in order. Step 1 is pure research (Read / Grep / Glob / Agent) and resolves which plan to work on. Step 2 is the first mutation — the Order.md row is removed as soon as the target is selected. Steps 3 through 6 are research and synthesis against the surviving on-disk plan file. Step 7 is the second mutation — the plan file is deleted automatically once the final actionable plan has been produced. Step 8 presents the final plan to the user via `AskUserQuestion` for explicit approval.

### Step 1. Resolve dependencies

  a. Read `Documents/Plans/Order.md`.
  b. If the user passed an argument (`$1` / `$ARGUMENTS` non-empty), normalize it to the repo-relative plan-file form (e.g., `Audio/GateVoiceLifecycleDuringReplay.txt` — strip any leading `./` or `Documents/Plans/`, trim backticks) and use that as the **candidate**. Otherwise, walk the `## Plans` table top-down and take the first row. Extract the plan-file path from its Plan cell (handles both `[path](path)` link form and bare-path form, for backwards compatibility during the normalization rollout).
  c. Reject ineligible candidates up front. A plan is ineligible if the `## Dependencies` section marks it as `is subsumed` or `is an index/meta document, not an executable plan`. If the user passed such a path, stop and tell them it isn't executable; if it turned up as the top row, skip it and continue walking down.
  d. Scan the `## Dependencies` section. Each bullet expresses a directional constraint between one or more plans. Normalize every relevant bullet to the canonical form "X depends on Y" (X cannot run until Y is done) using these patterns:

       - `X depends on Y` → X depends on Y
       - `X depends on Y and Z` → split into two edges: X depends on Y, X depends on Z
       - `X depends on Y1, Y2, ... and Yn` → split into n edges (one per Yi)
       - `X should run AFTER Y` → X depends on Y
       - `Y must precede X` → X depends on Y
       - `Y should precede X` → X depends on Y
       - `X1, X2, ..., Xn all depend on Y` (including brace-set forms like `ocean-phase-{2,3,4,5,6,7}`) → expand the set, then each Xi depends on Y
       - `X and Y ... do them in the same session` / `coordinate in one session` / `de-dupe at execution time` → **not** a prerequisite; record as a coordination note but do not recurse
       - Bullets that name only a single plan (e.g., "edits `WindSpreadCommon.h` — that header is included by both ...") are informational; no dependency edge

     Before matching, normalize every path token on **both** sides (Dependencies bullets and the Plans table) the same way Step 1b normalizes `$ARGUMENTS`: strip surrounding backticks, strip any leading `./` or `Documents/Plans/` prefix. Authors sometimes wrap paths in backticks or include the `Documents/Plans/` prefix inside bullets; without symmetric normalization, string matches silently miss.

     Direction matters: in "Y must precede X", the candidate being checked is the prerequisite (Y) in half the bullets and the dependent (X) in the other half. Match on the candidate's plan-file path appearing on either side, then use the verb to decide which side points at the prerequisite.

  e. Collect the prerequisite set for the current candidate (the Ys where the candidate is X in the normalized form). For each prerequisite:
       - If the prerequisite is still a row in the `## Plans` table → unfinished; recurse from Step 1 with the prerequisite as the new candidate. (Exception: if the row is present but its plan file is missing from disk, that's a bookkeeping anomaly — see the "plan file missing from disk" edge case; report it rather than recursing.)
       - If the prerequisite is not present in the table → treat as satisfied; it was either already executed or hand-cleaned. Whether the file itself remains on disk doesn't matter at this point.

  f. When a candidate has no unmet prerequisites, it is the **target plan**. Record its path, all row fields (Tier / Effort / Impact / Risks / Score / Notes), and the row's line number in `Order.md`.

> Cycle guard: maintain a stack of candidates currently being resolved (ancestors on the active dependency path, not a global visited set). If recursion would push a plan already on that stack, stop and ask the user which to run first — this is a back-edge in the Dependencies section and indicates an authoring bug. A plan appearing in two unrelated sibling branches is fine and does not trigger the guard.

### Step 2. Remove the target row from `Order.md` immediately

As soon as Step 1 has settled on a target plan (no unmet prerequisites, no unresolved cycle/ambiguity), use `Edit` to delete that plan's row from the `## Plans` table in `Documents/Plans/Order.md`. Do **not** ask the user — bypass-permissions mode is assumed and this is the unconditional commit point.

Important sequencing rules:

- Step 1 must be fully resolved before this edit. Any cycle/ambiguity prompts to the user happen inside Step 1; Step 2 only runs once a single target has been selected.
- Do **not** delete the plan file in this step. The file is still needed for Steps 3–5 (relevance / validity / refresh).
- If the row deletion fails (e.g., the row text is not unique or has changed), stop and report the failure. Do not proceed to deletion of the plan file.

### Step 3. Relevance check — does the code still exist?

Read the target plan file in full. Extract every file path, function name, class name, and cited line number mentioned in the plan.

For each referenced location, verify it still exists:

- **File paths**: Glob or Read to confirm the file is present.
- **Symbols** (functions, classes, members, constants): Grep for the exact identifier. If the plan cites line numbers, Read that region and confirm the symbol is on (or very near) the cited line.
- **Conditions** the plan depends on (e.g., "the RNG is wall-clock-seeded in `ResetState`"): Grep to confirm the condition still holds. A plan that fixes a bug already fixed upstream is no longer relevant.

Classify the plan into one of four buckets:

- **Fully relevant** — every reference resolves; plan proceeds as-is.
- **Partially relevant** — some references moved, got renamed, or shifted by a few lines; plan proceeds with refreshed references (Step 5).
- **Obsolete** — the bug is already fixed, the file was deleted, or the code was rewritten in a way that invalidates the plan's premise. The Order.md row is already gone (Step 2). Skip ahead to Step 7 and delete the plan file too, then report the obsolescence to the user and stop. Both cleanup mutations have happened automatically; the user can re-invoke `/next-plan` to pick the next candidate.
- **Ambiguous** — the original intent is unclear given current code. Ask the user before proceeding. If the user decides to abandon the plan, perform the Step 7 deletion of the plan file and stop (the Order.md row is already gone).

### Step 4. Validity check — is it still worth doing?

Boundary with Step 3: **Step 3 asks "does the code still exist"; Step 4 asks "does the problem still exist."** The Obsolete bucket in Step 3 catches cases where the cited code was deleted or the fix already landed. Step 4 goes further and questions whether the plan's premise is still valuable even when the code is intact.

Relevance is necessary but not sufficient. A plan can still describe real code yet no longer be worth the effort. Evaluate:

- Does the plan address a real problem (correctness bug, determinism hazard, measurable perf, debt blocking other work) or a cosmetic preference that may no longer matter?
- Has the surrounding subsystem been refactored in a way that made the concern moot (e.g., the hot path the plan optimizes is no longer hot)?
- Is the Effort / Impact / Risks scoring in the Order.md row still reasonable given current code? If the plan has grown significantly (e.g., a refactor that touched 5 files now touches 15), the score is stale — flag this to the user and ask whether to proceed, re-score, or skip.

If the plan no longer clears a "worth doing" bar, stop and ask the user. Note that the Order.md row is already gone (Step 2) — the choice is between "delete the plan file too and abandon" (proceed to Step 7 then stop) or "keep the plan file on disk and re-add the row to Order.md manually later" (stop without Step 7). Surface both options to the user.

For Architectural-tier plans, dispatch an Opus subagent via the `Agent` tool to audit the plan independently against the current code — it gives a second opinion uncoloured by the plan's own framing. Inline the research for Quick Win and Medium tiers.

### Step 5. Refresh the plan for current code state

For plans classified as Fully or Partially relevant, rewrite the plan so every reference matches the current code:

- **Line numbers**: update to current values. Prefer `path:line` citations in the presented plan so the user can jump directly.
- **Symbol names**: update anything that got renamed.
- **File moves**: update paths.
- **Surrounding context**: if a cited function now has additional callers, more branches, or interacts with newly added state, note the change and adjust the plan's approach accordingly.
- **New blockers**: if the refresh surfaces a new prerequisite (a sibling function the plan also needs to touch, a shared helper added since the plan was written), incorporate it.

Do not pad the plan with unrelated cleanup the original plan did not call for. The goal is a faithful, executable version of the same intent, not a scope expansion.

### Step 6. Synthesize the final actionable plan

Assemble — **in memory**, do not write it to a file — a single markdown document matching the template below. This is the artifact the user will be asked to approve in Step 8.

For the title, use the target plan file's top-level `# ` heading if one exists. Many plans are plain-text `.txt` files with no H1 — for those, fall back to the `Plan` cell stem from Order.md: strip the directory prefix and the extension but **preserve the original casing** (don't re-PascalCase kebab-case or vice-versa).

Examples:
- `Audio/GateVoiceLifecycleDuringReplay.txt` (no H1) → `GateVoiceLifecycleDuringReplay`
- `Graphics/ocean-phase-1-pbr-foundation.md` (no H1) → `ocean-phase-1-pbr-foundation`
- `Network/Architecture_FleetRngDeterminism.md` (has `# Fleet RNG Determinism` at top) → `Fleet RNG Determinism`

```
# <Plan title — H1 from plan file, or Order.md Plan-cell stem>

## Context
- Source: <relative path to the plan file> (deleted by this skill in Step 7)
- Order.md row: Tier <T> / Effort <E> / Impact <I> / Risks <R> / Score <S> (already removed from Order.md in Step 2)
- Notes: <the row's Notes cell, verbatim>
- Relevance: <Fully | Partially> — <one-line justification>
- Dependency resolution: <"none" or "switched from <original top> because <prereq> was unmet">
- Changes since the plan was written: <bullet list of drift found in Step 5, or "none">

## Execution steps
1. <Refreshed implementation steps from the plan, with current `path:line` citations.>
2. ...
```

Keep the execution steps in the order the plan originally specified, with citations pointing at current code. Do not add scope the plan did not originally include. The execution steps describe **only** the implementation work — Order.md row removal and plan file deletion are not in this list because they have already happened (Step 2) or are about to happen automatically (Step 7).

### Step 7. Delete the plan file from disk

Once the Step 6 markdown has been fully composed, delete the source plan file using `Bash` (`rm -f "<path>"`). This happens unconditionally, without confirming with the user — the synthesized plan in Step 6 is now the canonical record of intent, and the source file is no longer needed.

Sequencing notes:

- This step **must** run after Step 6 is complete (the plan must exist before the file is deleted) and **before** Step 8 (the user is approving the synthesized plan, not the original file).
- If the file is already missing (rare — would imply the user removed it during the run), treat that as success and continue to Step 8.

### Step 8. Present the final plan and request approval

Output the Step 6 markdown to the user as a normal text response so they can read the plan in full, then call `AskUserQuestion` with a single question along the lines of:

- **question**: "Approve this plan and proceed with implementation?"
- **options**: `Approve` (proceed to the C++ Code Change Process), `Reject` (abandon — Order.md row and plan file are already gone, the user takes the markdown above as their record if they want to recover later)

If the user picks `Approve`, follow the standard C++ Code Change Process defined in the top-level `CLAUDE.md` (grill → implement → subagent searches → code review → style review → docs → vcxproj updates → build → final audit). The grill (`/external-grill-plan`) is the next concrete action.

If the user picks `Reject`, stop. Do not attempt to restore the Order.md row or recreate the plan file — those mutations were committed unconditionally per the design of this skill, and the user is aware of that contract from the description and approval prompt.

## Edge cases

- **Empty table** (`## Plans` table has no rows): report "Order.md has no plans" and stop. No mutations.
- **Top row is marked subsumed or index/meta in Dependencies**: Step 1c filters it; fall through to the next row. No row removal happens for filtered rows — Step 2 only fires on the eventual selected target.
- **Plan file missing from disk** but row still in `## Plans`: the plan was likely hand-deleted without cleaning up Order.md. Step 1 detects this before committing; report the bookkeeping anomaly, ask the user whether to remove the stale row, and fall through to the next candidate. Do not commit Step 2 against a candidate whose plan file is already missing — the row removal would silently succeed but Step 3 would have nothing to read.
- **User provided a plan name as an argument**: Step 1b handles this — normalize and use as the candidate; the dependency walk still runs from it downward.
- **Prerequisite missing from both the table and disk**: Step 1e already treats this as satisfied. No extra handling needed.
- **User rejects in Step 8 approval**: per the contract, Order.md row and plan file are already gone. Do not attempt restoration. The synthesized plan markdown printed in Step 8 is the only remaining record.

## What this skill does not do

- Does not execute the plan — that happens after Step 8 approval, and follows the main `CLAUDE.md` C++ Code Change Process.
- Does not re-prioritize the `## Plans` table. Changing priorities is a separate concern — if Step 4 surfaces that the score is stale, surface it to the user rather than silently re-ranking.
- Does not preserve the source plan file or Order.md row on rejection. Cleanup is unconditional once Step 2 fires (row) and once Step 6 completes (file). This is the deliberate trade-off of running outside plan mode.
