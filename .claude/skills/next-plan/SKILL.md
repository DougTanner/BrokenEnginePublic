---
name: next-plan
description: First reconciles any orphaned plan files on disk that aren't referenced in `Documents/Plans/Order.md` by dispatching an Opus subagent to score them and inserting their rows. Then pulls the highest-priority plan, follows any unfinished prerequisites, validates it against the current codebase, refreshes stale details, scans the codebase for similar changes the plan may have missed, and presents a ready-to-execute plan for approval. Removes the Order.md row immediately on selection and auto-deletes the source plan file once the final actionable plan has been created. Use when the user invokes `/next-plan`.
disable-model-invocation: true
argument-hint: "[plan-file-path]"
allowed-tools: [Read, Grep, Glob, Agent, Edit, Bash, AskUserQuestion]
---

# Next Plan

Reconciles orphaned plan files on disk into `Documents/Plans/Order.md`, then walks the `## Plans` table, picks the top-priority unblocked plan, verifies it still describes a real problem in the current code, refreshes stale line numbers or paths, scans the codebase for similar changes the plan may have missed, and presents a ready-to-execute plan for explicit user approval.

## Preconditions

- The skill assumes **bypass-permissions** mode and mutates without further confirmation: it inserts `Order.md` rows for orphaned plan files (Step 0), removes the target row from `Documents/Plans/Order.md` as soon as the candidate is selected (Step 2), and deletes the source plan file from disk once the final actionable plan has been generated (Step 8). User approval is requested only on the final synthesized plan, not on the mutations.
- `Documents/Plans/Order.md` must exist. If it does not, report the missing file and stop.
- The skill does **not** require — and does not use — plan mode. It mimics plan mode's "review-before-implement" UX by printing the final plan as a standalone turn-ending text message (so it is readable and scrollable in the session window), then asking for approval via `AskUserQuestion` before the standard C++ Code Change Process begins. See Step 9 for the exact two-turn presentation contract.
- **Order.md is shared mutable state — expect concurrent writers.** The user routinely runs several agent sessions at once, each executing plans, so `Order.md` rows and plan files appear and vanish **while this skill is running**. Consequences:
  - Re-read the relevant `Order.md` region immediately before *every* edit (Step 0f inserts, Step 2 removal, any annotation); a snapshot from earlier in the run is already stale. If an `Edit` fails with "file modified since read", that is the expected concurrency signal — re-read and re-apply, don't escalate.
  - Key every row edit on the **plan path text**, never on a remembered line number — parallel inserts/removals shift lines constantly.
  - After any insert/remove, verify the table structure is still intact (header/separator present, every data row well-formed) before moving on.
  - A row (or plan file) that disappears mid-run is another session legitimately executing that plan — not corruption. Never "restore" it; see the edge case below.

## Order.md structure reference

Order.md has a single `## Plans` table. Columns are: `Plan | Tier | Effort | Impact | Risks | Score | Notes`. Rows are sorted by Score ascending (lowest = highest priority) — position in the table conveys priority; there is no ordinal column. Every row in the table is executable; rows are deleted from the table when the plan is done. Plan cells should be markdown links (`[path](path)`) for clickable navigation.

- **`### Reference / Index Documents` subsection**: a separate table below the main one, listing meta/overview docs that are never executed as plans. **Ignore this subsection entirely.**
- **`## Dependencies` section**: prose bullets expressing ordering constraints between plans. May contain a `### Cross-directory dependencies` subsection whose bullets cite plans under `Documents/Features/` — those live in a separate queue (`Documents/Features/Order.md`) and never appear in this table; see Step 1e for how to resolve them.
- **`## File Groups` section**: plans that touch the same files and should land in one session. Coordination info only — never a prerequisite edge; treat like the Step 1d coordination notes.

## Workflow

Execute these steps in order. Three unconditional mutation points (bypass-permissions assumed): Step 0 inserts rows for orphaned plan files, Step 2 removes the target's Order.md row the moment it is selected, and Step 8 deletes the plan file once the final actionable plan has been composed. Everything else is read-only research and synthesis; user approval happens only at Step 9, after both main-flow mutations.

### Step 0. Reconcile orphaned plan files into `Order.md`

Ensure every plan file on disk is represented in the `## Plans` table — orphans (present on disk, missing from the table) are otherwise invisible to the priority walk.

  a. **Enumerate plan files on disk.** Use `Glob` against `Documents/Plans/**/*.md` and `Documents/Plans/**/*.txt`. Normalize every hit to the Order.md-relative form (`<area>/<File>.<ext>`).

  b. **Build the exclusion set.** Skip:
       - `Documents/Plans/Order.md` itself and any `CLAUDE.md` under `Documents/Plans/`.
       - Every path listed under the `### Reference / Index Documents` subsection of `Order.md` (meta/overview docs that are never executed).
       - Every path referenced in the `## Plans` table's Plan cells (already in the queue). Extract paths from both link form (`[path](path)`) and bare-path form. Apply the same normalization as Step 1b.

  c. **Compute the orphan set.** Files from (a) that aren't in (b). If empty, log "no orphans" and skip to Step 1.

  d. **Dispatch a single Opus subagent via the `Agent` tool** to evaluate all orphans in one call. Brief it with:
       - The full list of orphan paths.
       - The scoring anchors from `Documents/CLAUDE.md` (Effort 1-5, Impact 1-5, Risks 0-4, Score = Effort − Impact + Risks; lower = higher priority).
       - The required row format from `Documents/Plans/CLAUDE.md`:
         `| [<area>/<File>.<ext>](<area>/<File>.<ext>) | <Tier> | <Effort> | <Impact> | <Risks> | <Score> | <one-line Notes> |`
       - The current `## Plans` table contents so it can pick a score-correct insertion position and write Notes consistent with neighbouring rows.
       - An instruction to read each orphan in full and spot-check the cited files/symbols in the codebase before scoring — a plan whose premise no longer exists should be flagged as "stale; recommend deletion" instead of getting a row.

  e. **Ask the subagent to return**, for each orphan:
       - The fully-populated table row (including final Score).
       - A target insertion position in the existing table (which existing row it goes immediately before or after, per the Score sort).
       - A label: `add` (insert into table) or `stale` (recommend the user delete the file — do not insert).
       - A one-paragraph justification for the scoring (kept out of `Order.md`, surfaced to the user in the post-Step-0 report).

  f. **Apply the additions.** For every `add` row, use `Edit` to insert it into the `## Plans` table at its score-correct position. Multiple inserts in a single Step 0 are independent single-row insertions — anchor each `Edit` on the neighbouring row's plan-path text, not on line numbers. Do not insert `stale` entries.

  g. **Report.** Output a brief summary to the user: which orphans were added (with score), which were flagged stale, and the subagent's justifications. Do not pause for confirmation — additions are unconditional, matching the skill's existing mutation contract. Stale flags are advisory only; the user can delete the files manually if they agree.

  h. Continue to Step 1. The orphan-reconciliation mutations are independent of the Step 1/Step 2 target selection — if Step 1 ends up choosing one of the just-inserted rows as its top candidate, that's fine.

### Step 1. Resolve dependencies

  a. Read `Documents/Plans/Order.md`.
  b. If the user passed an argument (`$1` / `$ARGUMENTS` non-empty), normalize it to the Order.md-relative plan-file form (e.g., `Audio/GateVoiceLifecycleDuringReplay.txt` — strip any leading `./` or `Documents/Plans/`, trim backticks) and use that as the **candidate**. Otherwise, walk the `## Plans` table top-down and take the first row. Extract the plan-file path from its Plan cell (handles both `[path](path)` link form and bare-path form, for backwards compatibility during the normalization rollout).
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
       - If the prerequisite path lives under `Documents/Features/` → it is tracked in `Documents/Features/Order.md`, outside this skill's queue. If it still has a row there, it is unfinished — do not recurse into it (this skill never executes Features plans); surface the blocker to the user and fall through to the next candidate (or stop and report, if the user passed the blocked plan as the argument). If it has no row there, treat as satisfied.
       - If the prerequisite is not present in the table → treat as satisfied; it was either already executed or hand-cleaned. Whether the file itself remains on disk doesn't matter at this point.

  f. When a candidate has no unmet prerequisites, it is the **target plan**. Record its path, all row fields (Tier / Effort / Impact / Risks / Score / Notes), and the row's line number in `Order.md`.

> Cycle guard: maintain a stack of candidates currently being resolved (ancestors on the active dependency path, not a global visited set). If recursion would push a plan already on that stack, stop and ask the user which to run first — this is a back-edge in the Dependencies section and indicates an authoring bug. A plan appearing in two unrelated sibling branches is fine and does not trigger the guard.

### Step 2. Remove the target row from `Order.md` immediately

As soon as Step 1 has settled on a target plan (no unmet prerequisites, no unresolved cycle/ambiguity), use `Edit` to delete that plan's row from the `## Plans` table in `Documents/Plans/Order.md`. Do **not** ask the user — bypass-permissions mode is assumed and this is the unconditional commit point.

Important sequencing rules:

- Step 1 must be fully resolved before this edit. Any cycle/ambiguity prompts to the user happen inside Step 1; Step 2 only runs once a single target has been selected.
- Re-read the row immediately before deleting and match on the plan path — a parallel session may have added or removed rows since Step 1 read the table (see Preconditions). This removal also acts as the concurrency claim on the target: once the row is gone, parallel `/next-plan` runs walking the table cannot select the same plan.
- If the row is already gone AND the plan file is missing, a parallel session beat this run to the same target — go back to Step 1 and select the next candidate from a fresh read of the table.
- Do **not** delete the plan file in this step. The file is still needed for Steps 3–7 (relevance / validity / refresh / similar-pattern search / synthesis).
- If the row deletion fails for any other reason (e.g., the row text is not unique), stop and report the failure. Do not proceed to deletion of the plan file.
- After the removal, verify the table structure is still intact before continuing.

### Step 3. Relevance check — does the code still exist?

Read the target plan file in full. Extract every file path, function name, class name, and cited line number mentioned in the plan.

For each referenced location, verify it still exists:

- **File paths**: Glob or Read to confirm the file is present.
- **Symbols** (functions, classes, members, constants): Grep for the exact identifier. If the plan cites line numbers, Read that region and confirm the symbol is on (or very near) the cited line.
- **Conditions** the plan depends on (e.g., "the RNG is wall-clock-seeded in `ResetState`"): Grep to confirm the condition still holds. A plan that fixes a bug already fixed upstream is no longer relevant.

Classify the plan into one of four buckets:

- **Fully relevant** — every reference resolves; plan proceeds as-is.
- **Partially relevant** — some references moved, got renamed, or shifted by a few lines; plan proceeds with refreshed references (Step 5).
- **Obsolete** — the bug is already fixed, the file was deleted, or the code was rewritten in a way that invalidates the plan's premise. The Order.md row is already gone (Step 2). Skip ahead to Step 8 and delete the plan file too, then report the obsolescence to the user and stop. Both cleanup mutations have happened automatically; the user can re-invoke `/next-plan` to pick the next candidate.
- **Ambiguous** — the original intent is unclear given current code. Ask the user before proceeding. If the user decides to abandon the plan, perform the Step 8 deletion of the plan file and stop (the Order.md row is already gone).

### Step 4. Validity check — is it still worth doing?

Boundary with Step 3: **Step 3 asks "does the code still exist"; Step 4 asks "does the problem still exist."** The Obsolete bucket in Step 3 catches cases where the cited code was deleted or the fix already landed. Step 4 goes further and questions whether the plan's premise is still valuable even when the code is intact.

Relevance is necessary but not sufficient. A plan can still describe real code yet no longer be worth the effort. Evaluate:

- Does the plan address a real problem (correctness bug, determinism hazard, measurable perf, debt blocking other work) or a cosmetic preference that may no longer matter?
- Has the surrounding subsystem been refactored in a way that made the concern moot (e.g., the hot path the plan optimizes is no longer hot)?
- Is the Effort / Impact / Risks scoring in the Order.md row still reasonable given current code? If the plan has grown significantly (e.g., a refactor that touched 5 files now touches 15), the score is stale — flag this to the user and ask whether to proceed, re-score, or skip.

If the plan no longer clears a "worth doing" bar, stop and ask the user. Note that the Order.md row is already gone (Step 2) — the choice is between "delete the plan file too and abandon" (proceed to Step 8 then stop) or "keep the plan file on disk and re-add the row to Order.md manually later" (stop without Step 8). Surface both options to the user.

For Architectural-tier plans, dispatch a Fable subagent via the `Agent` tool to audit the plan independently against the current code — it gives a second opinion uncoloured by the plan's own framing. Inline the research for all other tiers.

### Step 5. Refresh the plan for current code state

For plans classified as Fully or Partially relevant, rewrite the plan so every reference matches the current code:

- **Line numbers**: update to current values. Prefer `path:line` citations in the presented plan so the user can jump directly.
- **Symbol names**: update anything that got renamed.
- **File moves**: update paths.
- **Surrounding context**: if a cited function now has additional callers, more branches, or interacts with newly added state, note the change and adjust the plan's approach accordingly.
- **New blockers**: if the refresh surfaces a new prerequisite (a sibling function the plan also needs to touch, a shared helper added since the plan was written), incorporate it.

Do not pad the plan with unrelated cleanup the original plan did not call for. The goal is a faithful, executable version of the same intent, not a scope expansion. (Sibling instances of the same pattern — places the plan should arguably touch but doesn't — are surfaced separately in Step 6 rather than silently folded in here.)

### Step 6. Search the codebase for similar changes the plan may have missed

Dispatch an Opus subagent via the `Agent` tool to scan the codebase for additional locations that fit the plan's intent — places the original plan author overlooked, or that appeared in the codebase after the plan was written. The plan describes a specific transformation (a determinism fix in one file, a refactor of one collection, an allocation removed from one hot path); the subagent's job is to find sibling instances of the same pattern that would benefit from the same change.

Brief the subagent with:

- The full text of the target plan (refreshed in Step 5).
- The transformation pattern the plan implements, extracted from its execution steps: what it changes, where, and why. State this explicitly — "find every other place that does X" — rather than handing the subagent the raw plan and hoping it infers the pattern.
- Pointers into the codebase: the relevant subsystem `CLAUDE.md` files (`Engine/Source/CLAUDE.md`, `Common/CLAUDE.md`, `Projects/BrokenEngineSandbox/Source/CLAUDE.md`, etc.), the `Documents/Architecture/` Mermaid diagrams when the plan touches a diagrammed subsystem, and the aggregation headers (`Common/Common.h`, `Engine/Source/Engine.h`) for the affected namespace.
- An instruction to search BOTH directions: (a) **oversights** — code that already existed when the plan was written but was missed, and (b) **drift** — code added since the plan was written that exhibits the same pattern. The plan's age (commit history of the plan file or Order.md row) is useful context for distinguishing the two.

Ask the subagent to return:

- A list of candidate locations, each with `path:line`, the matching pattern, and a one-line justification for why the same change applies.
- For each candidate, a label: **Oversight** (was probably present when the plan was written) or **Drift** (likely added since).
- For each candidate, a **sameness** classification: **Identical** (the exact same mechanical transformation as the plan, differing only in file/location — e.g. the same unused-include removal, the same `Game.h` → `Graphics/Camera.h` swap, the same `vec + vec` → `XMVectorAdd` rewrite, the same field added to a sibling collection) or **Related** (same intent but a different edit — needs judgement about how to apply, touches different symbols, or carries invariant exposure the plan didn't declare).
- A confidence rating per candidate (high / medium / low) so the user can triage quickly.
- An explicit "no additional candidates" answer if the sweep finds nothing — the absence is informative and should still be recorded in Step 7's synthesized plan.

Handle the findings — record all of them in the Step 7 `## Additional candidate locations` section either way. The routing **default is to auto-fold**; surfacing for a user decision is the exception, reserved for candidates a thorough review cannot clear. Do not route on the sweep's `sameness`/`confidence` labels directly — they are triage hints for the review pass, not the gate. In particular, **Related** and medium/low-confidence candidates are no longer auto-surfaced; they go through the review gate like everything else and fold if it clears them.

**Extension-review gate — dispatch a second, thorough Opus subagent** via the `Agent` tool, dedicated solely to adversarially vetting every candidate for auto-folding before anything is folded. Brief it with the refreshed plan, the explicit transformation pattern, and each candidate's `path:line`. For each candidate it must independently, against current source:
  - (a) confirm it is genuinely the same transformation the plan implements (reject false positives),
  - (b) derive the exact edit and confirm it is mechanical and self-contained (no design choice, no ripple beyond the cited site), and
  - (c) determine whether the edit introduces **new invariant exposure the plan did not already declare** — new determinism/CRC participation, `kiVersion`/`.pack`/manifest layout, replay or save-format change, client/server guard scope, protocol/wire surface, or a main-loop allocation-tracked path.

  It returns, per candidate, exactly one verdict:
  - **Fold** — (a) and (b) confirmed and (c) is clean (within the plan's already-declared invariant envelope, or invariant-free). Auto-apply with no user ask, **regardless of Identical-vs-Related or confidence level**.
  - **Surface** — sameness or the exact edit could not be confirmed, the edit needs a design decision, or it carries new invariant exposure / architectural ambiguity / is plainly one slice of a larger superseding pattern.

Route strictly on that verdict:

- **Auto-fold** (no user ask) every **Fold** candidate. Append each to the plan's `## Execution steps` as an additional step tagged `[auto-folded sibling]`, and mark it **Folded** in `## Additional candidate locations` so the expansion stays visible and vetoable.
- **Surface for decision** only the **Surface** candidates. List these under `## Additional candidate locations` only — do **not** add them to `## Execution steps` — and let the user decide in Step 9 whether to fold them in, defer them to a follow-up plan, or ignore them. With the review gate doing the vetting this set is usually small or empty; an empty Surface set means **no Step 9 scope-expansion question at all**.

The thoroughness that used to come from reflexively surfacing uncertain candidates now lives in the extension-review gate above — fold aggressively once the gate clears a candidate, and only surface what the gate actually flags. The `[auto-folded sibling]` tag plus the **Folded** listing keep every auto-applied extension visible, and the C++ Code Change Process's post-implementation audit (step 9) is the backstop that re-reviews them in context.

If the subagent returns more than ~10 candidates, the plan likely describes a pattern broad enough to warrant a dedicated systematic sweep rather than a one-off fix. Note that observation in the synthesized plan and surface it to the user in Step 9 — do not bury dozens of candidates under an unrelated plan.

### Step 7. Synthesize the final actionable plan

Assemble — **in memory**, do not write it to a file — a single markdown document matching the template below. This is the artifact the user will be asked to approve in Step 9.

For the title, use the target plan file's top-level `# ` heading if one exists. Some plans are plain-text `.txt` files with no H1 — for those, fall back to the `Plan` cell stem from Order.md: strip the directory prefix and the extension but **preserve the original casing** (don't re-PascalCase kebab-case or vice-versa).

Examples:
- `Audio/GateVoiceLifecycleDuringReplay.txt` (no H1) → `GateVoiceLifecycleDuringReplay`
- `Graphics/ocean-phase-1-pbr-foundation.md` (no H1) → `ocean-phase-1-pbr-foundation`
- `Network/Architecture_FleetRngDeterminism.md` (has `# Fleet RNG Determinism` at top) → `Fleet RNG Determinism`

```
# <Plan title — H1 from plan file, or Order.md Plan-cell stem>

## Summary
**What this plan does:** <2-4 sentences in plain prose describing the change in concrete terms. Name the subsystems / files touched and the user- or engine-visible behavior change. Avoid restating the title; avoid step-by-step detail (that lives in Execution steps).>

**Why it's good for the codebase:** <2-4 sentences naming the concrete benefit. Pick from: correctness bug fixed, determinism hazard closed, measurable perf win, debt removed that unblocks <named follow-up>, simplification that deletes <N> lines / removes <named abstraction>, hot-path allocation eliminated, etc. Be specific — "improves code quality" is not acceptable; "removes the per-frame heap allocation in `BlasterPostRender::Update` flagged by allocation tracking" is.>

## Context
- Source: <relative path to the plan file> (deleted by this skill in Step 8)
- Order.md row: Tier <T> / Effort <E> / Impact <I> / Risks <R> / Score <S> (already removed from Order.md in Step 2)
- Notes: <the row's Notes cell, verbatim>
- Relevance: <Fully | Partially> — <one-line justification>
- Dependency resolution: <"none" or "switched from <original top> because <prereq> was unmet">
- Changes since the plan was written: <bullet list of drift found in Step 5, or "none">

## Execution steps
1. <Refreshed implementation steps from the plan, with current `path:line` citations.>
2. ...

## Additional candidate locations
<Findings from the Step 6 codebase sweep — sibling instances of the same pattern the plan may have missed. One bullet per candidate, with `path:line`, label (**Oversight** | **Drift**), sameness (**Identical** | **Related**), confidence (high/medium/low), routing (**Folded** — auto-folded into `## Execution steps`; or **Surfaced** — awaiting the user's Step 9 decision), and a one-line justification. If the sweep returned nothing, write "No additional candidates found." If it returned more than ~10 candidates, note that the pattern likely warrants a dedicated systematic sweep rather than expanding this plan.>
```

Authoring the **Summary** section is mandatory and must come from synthesis, not boilerplate. Source the **What** from the plan's body (its goal statement, top-level description, or the union of its execution steps if no narrative exists) and the **Why** from a combination of the plan file's stated rationale and the Order.md row's `Impact` / `Notes` cells. If the plan file contains no rationale at all, infer the Why from the code drift uncovered during Steps 3-5 and prefix the sentence with "Inferred:" so the user knows it isn't author-supplied. Never write a generic Why like "improves quality" or "cleans up the codebase" — if you cannot name a concrete benefit, surface that gap to the user in Step 9 instead of papering over it.

Keep the execution steps in the order the plan originally specified, with citations pointing at current code. Do not add **Related** or uncertain scope the plan did not originally include — that lives in `## Additional candidate locations`, not `## Execution steps`. The one exception is the **auto-folded Identical siblings** from Step 6 (same mechanical change, different location, high-confidence, exposure-free): append those to `## Execution steps` tagged `[auto-folded sibling]`. Order.md row removal and plan file deletion are not in the execution list because they have already happened (Step 2) or are about to happen automatically (Step 8).

### Step 8. Delete the plan file from disk

Once the Step 7 markdown has been fully composed, delete the source plan file using `Bash` (`rm -f "<path>"`). This happens unconditionally, without confirming with the user — the synthesized plan in Step 7 is now the canonical record of intent, and the source file is no longer needed.

Sequencing notes:

- This step **must** run after Step 7 is complete (the plan must exist before the file is deleted) and **before** Step 9 (the user is approving the synthesized plan, not the original file).
- If the file is already missing (rare — would imply the user removed it during the run), treat that as success and continue to Step 9.

### Step 9. Present the final plan and request approval

**The plan and its rationale MUST land in the session context window before approval is requested — the user must be able to scroll up and read the full plan text in the transcript while (and after) deciding.** Text sandwiched between tool calls, or emitted in the same assistant message as a tool call, is not reliably rendered to the user; an `AskUserQuestion` option `preview` pane is not scrollback and does not satisfy this requirement on its own.

Concretely, present in two turns:

  1. Output the Step 7 markdown as the **final text of the turn, with no tool calls in that message and none after it**, so it renders fully in the session window. End the turn there.
  2. When the user responds (any acknowledgement — the content of their reply doesn't matter unless it raises questions), call `AskUserQuestion` for the approval. Optionally duplicate the plan in the `Approve` option's `preview` as a convenience copy, but never as the only copy.

If the user's reply to (1) already contains an unambiguous decision ("approved", "reject", "skip the TextureCache one"), honor it directly and skip or trim the `AskUserQuestion` accordingly.

The `AskUserQuestion` is a single question along the lines of:

- **question**: "Approve this plan and proceed with implementation?"
- **options**: `Approve` (proceed to the C++ Code Change Process), `Reject` (abandon — Order.md row and plan file are already gone, the user takes the markdown above as their record if they want to recover later)

If the `## Additional candidate locations` section contains any **Surfaced** entries (only those the Step 6 extension-review gate flagged as carrying new invariant exposure, needing a design decision, or unconfirmable — everything else was auto-folded), ask the user whether to fold them into the execution scope, defer them to a follow-up plan, or ignore them. Use a separate `AskUserQuestion` call (or a multi-select question) so the approval decision and the scope-expansion decision are tracked independently. Do **not** ask about auto-folded candidates — those are already in `## Execution steps` by design; just mention them in the presentation so the user can veto if they disagree, but don't gate on it. Because the review gate now clears the routine cases automatically, the Surface set is usually empty — when it is (or when every candidate was auto-folded), skip the scope-expansion question entirely.

If the user picks `Approve`, follow the standard C++ Code Change Process defined in the top-level `CLAUDE.md` (grill → implement → subagent searches → code review → style review → docs → vcxproj updates → build → final audit). The grill (`/external-grill-plan`) is the next concrete action. Carry the user's decisions on additional candidates into the grill so the implementation reflects the agreed scope.

**Do not stop after the grill returns.** The grill's contract is to interrogate the plan and silently update it when it has questions; if it walks every branch and finds no decision points needing user input (common for trivial single-line refactors and dead-code deletions), it returns control with nothing to summarise. That return is **not** a checkpoint — it is the handoff into Step 2 of the C++ Code Change Process (make the code changes). Proceed to the edit immediately in the same turn. The only legitimate reasons to pause after the grill are (a) the grill itself asked the user a question that is still open, or (b) the grill explicitly recommended running `/external-design-interface` first per its role-boundary clause. Otherwise, edit.

If the user picks `Reject`, stop. Do not attempt to restore the Order.md row or recreate the plan file — those mutations were committed unconditionally per the design of this skill, and the user is aware of that contract from the description and approval prompt.

## Edge cases

- **No orphans found in Step 0**: skip the subagent dispatch entirely and proceed to Step 1. No mutations.
- **Step 0 subagent flags every orphan as stale**: no rows inserted; report the stale list to the user and continue to Step 1 against the unchanged table.
- **Orphan file lives under a subdirectory the table doesn't yet reference** (e.g., a new `Audio/` area): the row goes in at score-correct position regardless of subdirectory — the table is sorted by Score, not grouped by area.
- **Orphan is itself a Reference / Index document** that wasn't added to the `### Reference / Index Documents` subsection: Step 0b only excludes paths already listed there, so a genuinely-meta doc would be misclassified as a plan. The Opus subagent should detect this from the document's content (no execution steps, no `## Critical files` section, narrative overview tone) and label it `stale` with a justification recommending the user move it to the reference subsection.
- **Empty table** (`## Plans` table has no rows): report "Order.md has no plans" and stop. No mutations.
- **Top row is marked subsumed or index/meta in Dependencies**: Step 1c filters it; fall through to the next row. No row removal happens for filtered rows — Step 2 only fires on the eventual selected target.
- **Plan file missing from disk** but row still in `## Plans`: most often a parallel session mid-execution of that plan (its Step 8 file deletion has fired but you read the table before its Step 2 removal landed, or vice versa) — re-read the table; if the row is gone on the fresh read, fall through to the next candidate silently. If the row persists across a fresh read, the plan was likely hand-deleted without cleaning up Order.md: report the bookkeeping anomaly, ask the user whether to remove the stale row, and fall through to the next candidate. Either way, do not commit Step 2 against a candidate whose plan file is already missing — the row removal would silently succeed but Step 3 would have nothing to read.
- **Row and/or plan file disappears mid-run (concurrent execution)**: many plans are in flight at once across parallel agent sessions, so a row + file vanishing together is another session legitimately executing that plan — not corruption. Never restore the row or recreate the file. If it was this run's candidate, restart from Step 1 on a fresh read; if it was an unrelated row, just re-verify the table structure is intact and continue. Mention the observation in the final report so the user can correlate sessions.
- **User provided a plan name as an argument**: Step 1b handles this — normalize and use as the candidate; the dependency walk still runs from it downward.
- **Prerequisite missing from both the table and disk**: Step 1e already treats this as satisfied. No extra handling needed.
- **User rejects in Step 9 approval**: per the contract, Order.md row and plan file are already gone. Do not attempt restoration. The synthesized plan markdown printed in Step 9 is the only remaining record.
- **Step 6 sweep finds an obviously-superseding plan**: if the codebase sweep finds that the plan is one instance of a much larger pattern that has its own existing plan in `Order.md`, surface that to the user in Step 9 so they can decide whether to abandon the current plan in favor of the broader one.

## What this skill does not do

- Does not execute the plan — that happens after Step 9 approval, and follows the main `CLAUDE.md` C++ Code Change Process.
- Does not re-prioritize the `## Plans` table. Changing priorities is a separate concern — if Step 4 surfaces that the score is stale, surface it to the user rather than silently re-ranking.
- Does not preserve the source plan file or Order.md row on rejection. Cleanup is unconditional once Step 2 fires (row) and once Step 8 fires (file). This is the deliberate trade-off of running outside plan mode.
- Does not blindly expand scope, but **does auto-fold aggressively**: every Step 6 candidate that the dedicated extension-review subagent clears (confirmed same transformation, mechanical, no new undeclared invariant exposure) is folded into `## Execution steps` with no user ask, tagged `[auto-folded sibling]` and listed **Folded** under `## Additional candidate locations` so it stays visible and vetoable. This includes **Related** and lower-confidence siblings — the thorough review gate, not a reflexive user prompt, is what vets them. Only candidates the review gate flags (new invariant exposure, a needed design decision, or unconfirmable sameness) are surfaced for an explicit Step 9 decision.
