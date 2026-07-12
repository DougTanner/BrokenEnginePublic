---
name: next-plan
description: First reconciles any orphaned plan files on disk that aren't referenced in `Documents/Plans/Order.md` by dispatching an Opus subagent to score them and inserting their rows. Then pulls the highest-priority plan, follows any unfinished prerequisites, validates it against the current codebase, refreshes stale details, scans the codebase for similar changes the plan may have missed, audits and grills the plan up front, and presents a ready-to-execute plan for approval. Claims the selected plan with a PC-global atomic lock and mirrors it as `[CLAIMED <date>]` in Order.md; the row and plan file are removed only after the plan is fully executed, and rejection lands the refreshed queued plan before releasing the claim. Use when the user invokes `/next-plan`.
disable-model-invocation: true
argument-hint: "[plan-file-path]"
allowed-tools: [Read, Write, Grep, Glob, Agent, Edit, PowerShell, AskUserQuestion]
---

# Next Plan

Reconciles orphaned plan files on disk into `Documents/Plans/Order.md`, then walks the `## Plans` table, picks the top-priority unblocked plan, verifies it still describes a real problem in the current code, refreshes stale line numbers or paths, scans the codebase for similar changes the plan may have missed, audits and grills the plan up front, and presents a ready-to-execute plan for explicit user approval.

## Preconditions

- The skill assumes **bypass-permissions** mode and mutates without further confirmation: it inserts orphan rows (Step 0), atomically creates the target's PC-global claim and mirrors `[CLAIMED <date>]` locally (Step 2), and overwrites the plan file with the synthesis (Step 7). Row/file cleanup and owner-checked claim release happen only through Step 8. User approval is requested only on the final synthesized plan.
- `Documents/Plans/Order.md` must exist. If it does not, report the missing file and stop.
- The skill does **not** require — and does not use — plan mode. It mimics plan mode's "review-before-implement" UX by auditing (`/plan-audit`) then grilling (`/external-grill-plan`) the plan up front, then printing the final refined plan once as a standalone turn-ending text message (readable and scrollable in the session window). After that presentation, an unambiguous `Approve` or "execute" jumps straight into implementation (C++ Code Change Process step 2) with no further interview or approval prompt. Audit and grill together are C++ Code Change Process step 1. See Step 9 for the exact audit-grill-present-approve contract.
- **`/next-plan` requires an existing isolated worktree.** If the session is not already in one, report that precondition and stop; never create one inside the skill. All subagents share that checkout and the fixed session-start commit is the changed-file baseline. Sessions coordinate through AgentCli's PC-global claim and landing locks, not checkout-local queue edits.
- Re-read the relevant `Order.md` region before every edit, key edits on plan-path text, and verify the table structure afterwards. At landing, reconcile the latest primary-branch version semantically; do not overwrite another session's queue changes.
- Set `$AgentCli` to installed `%LOCALAPPDATA%\BrokenEngine\AgentCli\v2\AgentCli.exe`; `--version` must print exactly `2`. If missing or mismatched, build `Tools/AgentCli/Platforms/VisualStudio2026/AgentCli.sln` Release/x64 directly with native PowerShell using `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe` (`vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath` fallback), preserving the code-analysis-disable properties. Run the Release output's `install` mode, then require installed `--version` to print `2`; the `/compile` skill contains the exact bootstrap block.
- The authoritative per-plan claim is `AgentCli lock claim --domain plan --key <normalized Order.md-relative path>`. Generate one owner token with `lock token` immediately before claiming and retain it through release. Record the session label and resolved worktree in claim metadata. Any `[CLAIMED ...]` text in local `Order.md` is informational only.
- Claim age is warning-only. Never auto-steal a paused claim; takeover requires explicit user approval and `lock steal --expect <reported-owner>`. Release only with this session's owner token.

## Order.md structure reference

Order.md has a single `## Plans` table. Columns are: `Plan | Tier | Effort | Impact | Risks | Score | Notes`. Rows are sorted by Score ascending (lowest = highest priority) — position in the table conveys priority; there is no ordinal column. Every row in the table is executable; rows are deleted from the table when the plan is done. Plan cells should be markdown links (`[path](path)`) for clickable navigation. A Notes cell beginning with `[CLAIMED <date>]` mirrors a claim for readers, but only AgentCli lock ownership is authoritative.

- **`### Reference / Index Documents` subsection**: a separate table below the main one, listing meta/overview docs that are never executed as plans. **Ignore this subsection entirely.**
- **`## Dependencies` section**: prose bullets expressing ordering constraints between plans. May contain a `### Cross-directory dependencies` subsection whose bullets cite plans under `Documents/Features/` — those live in a separate queue (`Documents/Features/Order.md`) and never appear in this table; see Step 1e for how to resolve them.
- **`## File Groups` section**: plans that touch the same files. This is warning and landing-order information only — never a prerequisite edge.

## Workflow

Execute these steps in order. Mutation points (bypass-permissions assumed): Step 0 inserts rows for orphaned plan files, Step 2 atomically claims the target and mirrors `[CLAIMED <date>]` in its row, Step 7 overwrites the plan file with the synthesized plan, and Step 8 defines completion and claim release. Everything else is read-only research and synthesis; the plan grill and user approval both happen at Step 9 (grill first, then approval).

### Step 0. Reconcile orphaned plan files into `Order.md`

Ensure every plan file on disk is represented in the `## Plans` table — orphans (present on disk, missing from the table) are otherwise invisible to the priority walk.

  a. **Enumerate plan files on disk.** Use `Glob` against `Documents/Plans/**/*.md` and `Documents/Plans/**/*.txt`. Normalize every hit to the Order.md-relative form (`<area>/<File>.<ext>`).

  b. **Build the exclusion set.** Skip:
       - `Documents/Plans/Order.md` itself and any `AGENTS.md` or `CLAUDE.md` under `Documents/Plans/`.
       - Every path listed under the `### Reference / Index Documents` subsection of `Order.md` (meta/overview docs that are never executed).
       - Every path referenced in the `## Plans` table's Plan cells (already in the queue). Extract paths from both link form (`[path](path)`) and bare-path form. Apply the same normalization as Step 1b.

  c. **Compute the orphan set.** Files from (a) that aren't in (b). If empty, log "no orphans" and skip to Step 1.

  d. **Dispatch a single Opus subagent via the `Agent` tool** to evaluate all orphans in one call. Brief it with:
       - The full list of orphan paths.
       - The scoring anchors from `Documents/AGENTS.md` (Effort 1-5, Impact 1-5, Risks 0-4, Score = Effort − Impact + Risks; lower = higher priority).
       - The required row format from `Documents/Plans/AGENTS.md`:
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
  b. If the user passed an argument (`$1` / `$ARGUMENTS` non-empty), normalize it to the Order.md-relative plan-file form (e.g., `Audio/GateVoiceLifecycleDuringReplay.txt` — strip any leading `./` or `Documents/Plans/`, trim backticks) and use that as the **candidate**. Otherwise, walk the `## Plans` table top-down and take the first candidate for which `& $AgentCli lock status --domain plan --key <normalized-plan-path>` reports no claim. A claim means another session owns it regardless of Notes; any Notes marker without a claim is informationally stale and produces a warning, not a block. Extract plan paths from either link or bare-path form.
  c. Reject ineligible candidates up front. A plan is ineligible if the `## Dependencies` section marks it as `is subsumed` or `is an index/meta document, not an executable plan`. If the user passed such a path, stop and tell them it isn't executable; if it turned up as the top row, skip it and continue walking down.
  d. Scan the `## Dependencies` section. Each bullet expresses a directional constraint between one or more plans. Normalize every relevant bullet to the canonical form "X depends on Y" (X cannot run until Y is done) using these patterns:

       - `X depends on Y` → X depends on Y
       - `X depends on Y and Z` → split into two edges: X depends on Y, X depends on Z
       - `X depends on Y1, Y2, ... and Yn` → split into n edges (one per Yi)
       - `X should run AFTER Y` → X depends on Y
       - `Y must precede X` → X depends on Y
       - `Y should precede X` → X depends on Y
       - `resolve Y first` / `resolve Y before X` → X depends on Y when X is the other named plan or set in that entry
       - `land X after Y` → X depends on Y
       - `land Y before X` → X depends on Y
       - `land X first` → every other plan explicitly named in that entry depends on X
       - `X is a prerequisite for Y` → Y depends on X
       - `X last` / `X runs last` → X depends on every other plan explicitly named in that entry
       - `X1, X2, ..., Xn all depend on Y` (including brace-set forms like `ocean-phase-{2,3,4,5,6,7}`) → expand the set, then each Xi depends on Y
       - `X and Y ... do them in the same session` / `coordinate in one session` / `de-dupe at execution time` / `co-schedule` / shared-file or refresh-citation overlap → **not** a prerequisite; record the intersecting files and expected landing order as a warning, but do not recurse
       - `never interleave`, `conflicts`, `resolve jointly before landing either`, `executes alone`, `runs alone`, or protocol-version, CRC/replay, `.pack`, or `kiVersion` batching → **not** a prerequisite unless the entry also gives directional prerequisite language; record it as a mandatory landing constraint that the later lander must preserve, reconcile, and reverify, not as an ordinary warning
       - Bullets that name only a single plan (e.g., "edits `WindSpreadCommon.h` — that header is included by both ...") are informational; no dependency edge

     Before matching, normalize every path token on **both** sides (Dependencies bullets and the Plans table) the same way Step 1b normalizes `$ARGUMENTS`: strip surrounding backticks, strip any leading `./` or `Documents/Plans/` prefix. Authors sometimes wrap paths in backticks or include the `Documents/Plans/` prefix inside bullets; without symmetric normalization, string matches silently miss.

     Direction matters: in "Y must precede X", the candidate being checked is the prerequisite (Y) in half the bullets and the dependent (X) in the other half. Match on the candidate's plan-file path appearing on either side, then use the verb to decide which side points at the prerequisite. Do not infer an edge from ordinary `refresh citations`, `refresh after`, `co-schedule`, or shared-file wording unless the same entry also uses one of the explicit directional forms above.

  e. Collect the prerequisite set for the current candidate (the Ys where the candidate is X in the normalized form). For each prerequisite:
       - If the prerequisite is still a row in the `## Plans` table → unfinished. If AgentCli reports a plan-domain claim for it, do not recurse: the original top-level candidate is blocked. During the automatic walk, return to the caller and continue with the next top-level eligible row; for an explicitly requested candidate, report the blocker and stop. Include claim metadata and never treat claim age as release authority. Otherwise recurse from Step 1 with the prerequisite as the new candidate. (Exception: if the row is present but its plan file is missing from disk, report the bookkeeping anomaly rather than recursing.)
       - If the prerequisite path lives under `Documents/Features/` → it is tracked in `Documents/Features/Order.md`, outside this skill's queue. If it still has a row there, it is unfinished — do not recurse into it (this skill never executes Features plans); surface the blocker to the user and fall through to the next candidate (or stop and report, if the user passed the blocked plan as the argument). If it has no row there, treat as satisfied.
       - If the prerequisite is not present in the table → treat as satisfied; it was either already executed or hand-cleaned. Whether the file itself remains on disk doesn't matter at this point.

  f. When a candidate has no unmet prerequisites, it is the **target plan**. Record its path and row fields (Tier / Effort / Impact / Risks / Score / Notes). Also collect ordinary warning-only overlaps from `## Dependencies` and `## File Groups`; report the other plan/session, intersecting files, and likely landing order. Separately report every `never interleave`, conflict/joint-resolution, alone-execution, and protocol/version/CRC/replay/`.pack`/`kiVersion` landing constraint so it survives synthesis and landing. Ordinary overlap and nondirectional landing constraints do not change selection; explicit prerequisites do.

> Cycle guard: maintain a stack of candidates currently being resolved (ancestors on the active dependency path, not a global visited set). If recursion would push a plan already on that stack, stop and ask the user which to run first — this is a back-edge in the Dependencies section and indicates an authoring bug. A plan appearing in two unrelated sibling branches is fine and does not trigger the guard.

### Step 2. Atomically claim the target plan

As soon as Step 1 settles on a target, use its normalized plan path as the unified key:

```powershell
$Owner = & $AgentCli lock token
& $AgentCli lock claim --domain plan --key $TargetPlan --owner $Owner --session '<short task label>' --worktree $ROOT
```

Exit code `0` reserves the plan; an `Order.md` edit does not. Retain `$Owner` through completion or rejection.

Important sequencing rules:

- Step 1 must be fully resolved before claiming. Any cycle/ambiguity prompts happen inside Step 1.
- If claim returns exit code `2`, run `lock status` with the same locator. For automatic selection, return to Step 1 and choose the next candidate. For an explicitly requested plan, report the owner and ask whether the user approves takeover. Age only triggers a warning. On approval, copy the reported owner and run `lock steal --domain plan --key $TargetPlan --expect <reported-owner> --owner $Owner --session '<label>' --worktree $ROOT`; abort or retry selection if ownership changed.
- After acquiring the claim, re-read the row and prefix its Notes with `[CLAIMED <local-date>] `, keeping the rest intact. Replace any existing claim marker rather than stacking markers. This marker is informational; a mirror-edit failure does not transfer ownership. Report and repair it without releasing the claim.
- In an isolated session worktree, another checkout cannot make the local row/file disappear. If either vanishes before the mirror edit, inspect local edits and action history first. Only if this is post-reconciliation and the rebased primary commit proves another session completed the plan may this session owner-check and release its redundant claim, then restart Step 1; otherwise stop and report the unexplained local mutation.
- Do **not** remove the row or delete the plan file in this step. Both survive until the Step 8 completion contract fires; the file is needed for Steps 3–7 (relevance / validity / refresh / similar-pattern search / synthesis) and through implementation.
- After the edit, verify the table structure is still intact before continuing.

### Step 3. Relevance check — does the code still exist?

Read the target plan file in full. Extract every file path, function name, class name, and cited line number mentioned in the plan.

For each referenced location, verify it still exists:

- **File paths**: Glob or Read to confirm the file is present.
- **Symbols** (functions, classes, members, constants): Grep for the exact identifier. If the plan cites line numbers, Read that region and confirm the symbol is on (or very near) the cited line.
- **Conditions** the plan depends on (e.g., "the RNG is wall-clock-seeded in `ResetState`"): Grep to confirm the condition still holds. A plan that fixes a bug already fixed upstream is no longer relevant.

Classify the plan into one of four buckets:

- **Fully relevant** — every reference resolves; plan proceeds as-is.
- **Partially relevant** — some references moved, got renamed, or shifted by a few lines; plan proceeds with refreshed references (Step 5).
- **Obsolete** — the bug is already fixed, the file was deleted, or the code was rewritten in a way that invalidates the plan's premise. Obsolescence is a terminal state: run the Step 8 completion cleanup now (remove the claimed row, delete the plan file), report the obsolescence to the user, and stop. The user can re-invoke `/next-plan` to pick the next candidate.
- **Ambiguous** — the original intent is unclear given current code. Ask the user before proceeding. If the user decides to abandon the plan, ask whether to clean it up or return it to the queue; both routes use Step 8's owner-checked mirror/claim release, and only cleanup removes the row/file.

### Step 4. Validity check — is it still worth doing?

Boundary with Step 3: **Step 3 asks "does the code still exist"; Step 4 asks "does the problem still exist."** The Obsolete bucket in Step 3 catches cases where the cited code was deleted or the fix already landed. Step 4 goes further and questions whether the plan's premise is still valuable even when the code is intact.

Relevance is necessary but not sufficient. A plan can still describe real code yet no longer be worth the effort. Evaluate:

- Does the plan address a real problem (correctness bug, determinism hazard, measurable perf, debt blocking other work) or a cosmetic preference that may no longer matter?
- Has the surrounding subsystem been refactored in a way that made the concern moot (e.g., the hot path the plan optimizes is no longer hot)?

Treat the Order.md Tier / Effort / Impact / Risks / Score fields as fixed estimates used only to order the queue. Do not reassess or re-score them while selecting or executing a plan.

**"Worth doing" rubric** — the plan clears the bar if ANY of:

- it fixes a correctness/determinism hazard still present in current code;
- it delivers measurable perf on a path still hot (check against the most recent capture/baseline the plan or its Notes cite);
- it unblocks a named queued plan or feature (a `## Dependencies`/`## File Groups` edge, or explicit in the plan body);
- it deletes a real abstraction or ≥ ~100 lines.

It fails the bar when the concern is now merely cosmetic or modest, no queued plan depends on it, and the work has materially expanded; it also fails when another queued plan supersedes or will rewrite the same code — surface that plan instead.

If the plan no longer clears the bar, stop and ask the user. The choice is between "abandon" (Step 8 removes row/file and releases the owner-verified claim) or "return it to the queue" (remove this session's dated mirror, release its owner-verified claim, and keep the file). Surface both options.

For Architectural-tier plans, dispatch an Opus subagent via the `Agent` tool to audit the plan independently against the current code — it gives a second opinion uncoloured by the plan's own framing. Inline the research for all other tiers.

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
- Pointers into the codebase: the relevant subsystem `AGENTS.md` files (`Engine/Source/AGENTS.md`, `Common/AGENTS.md`, `Projects/BrokenEngineSandbox/Source/AGENTS.md`, etc.), the `Documents/Architecture/` Mermaid diagrams when the plan touches a diagrammed subsystem, and the aggregation headers (`Common/Common.h`, `Engine/Source/Engine.h`) for the affected namespace.
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

The thoroughness that used to come from reflexively surfacing uncertain candidates now lives in the extension-review gate above — fold aggressively once the gate clears a candidate, and only surface what the gate actually flags. The `[auto-folded sibling]` tag plus the **Folded** listing keep every auto-applied extension visible, and the C++ Code Change Process's post-implementation audit is the backstop that re-reviews them in context.

If the subagent returns more than ~10 candidates, the plan likely describes a pattern broad enough to warrant a dedicated systematic sweep rather than a one-off fix. Note that observation in the synthesized plan and surface it to the user in Step 9 — do not bury dozens of candidates under an unrelated plan.

### Step 7. Synthesize the final actionable plan

Assemble a single markdown document matching the template below, then `Write` it over the source plan file — the refreshed plan replaces the original as the canonical record: `/external-grill-plan` updates this file with its resolved answers, the implementation subagent reads it, and it stays recoverable if the run is rejected. This is also the artifact the user will be asked to approve in Step 9.

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
- Source: <relative path to the plan file> (claimed; removed with its Order.md row after execution completes)
- Order.md row: Tier <T> / Effort <E> / Impact <I> / Risks <R> / Score <S> (mirrored as `[CLAIMED <date>]` in Step 2)
- Notes: <the row's Notes cell, verbatim>
- Relevance: <Fully | Partially> — <one-line justification>
- Dependency resolution: <"none" or "switched from <original top> because <prereq> was unmet">
- Coordination warnings: <"none" or each warning-only overlap with other plan/session, intersecting files, and likely landing order>
- Mandatory landing constraints: <"none" or every `never interleave`, conflict/joint-resolution, alone-execution, protocol/version, CRC/replay, `.pack`, and `kiVersion` constraint that implementation and landing must preserve and reverify>
- Changes since the plan was written: <bullet list of drift found in Step 5, or "none">

## Execution steps
1. <Refreshed implementation steps from the plan, with current `path:line` citations.>
2. ...

## Additional candidate locations
<Findings from the Step 6 codebase sweep — sibling instances of the same pattern the plan may have missed. One bullet per candidate, with `path:line`, label (**Oversight** | **Drift**), sameness (**Identical** | **Related**), confidence (high/medium/low), routing (**Folded** — auto-folded into `## Execution steps`; or **Surfaced** — awaiting the user's Step 9 decision), and a one-line justification. If the sweep returned nothing, write "No additional candidates found." If it returned more than ~10 candidates, note that the pattern likely warrants a dedicated systematic sweep rather than expanding this plan.>
```

Authoring the **Summary** section is mandatory and must come from synthesis, not boilerplate. Source the **What** from the plan's body (its goal statement, top-level description, or the union of its execution steps if no narrative exists) and the **Why** from a combination of the plan file's stated rationale and the Order.md row's `Impact` / `Notes` cells. If the plan file contains no rationale at all, infer the Why from the code drift uncovered during Steps 3-5 and prefix the sentence with "Inferred:" so the user knows it isn't author-supplied. Never write a generic Why like "improves quality" or "cleans up the codebase" — if you cannot name a concrete benefit, surface that gap to the user in Step 9 instead of papering over it.

Keep the execution steps in the order the plan originally specified, with citations pointing at current code. Append every Step 6 candidate whose extension-review verdict is **Fold** to `## Execution steps` tagged `[auto-folded sibling]`, regardless of Identical/Related or confidence. Keep every **Surface** candidate out of execution pending Step 9. Preserve the Context coordination warnings and mandatory landing constraints through implementation and landing; the later lander reruns every affected review, build, and verification step. Order.md row removal and plan-file deletion belong to Step 8, not the execution list.

### Step 8. Completion contract — cleanup and owner-checked release

Nothing is deleted at selection time. The claimed row and plan file are removed together at exactly one of these points:

Before invoking `/finalize-changes` on every completion, terminal-abandonment, or rejection route, run two independent `/session-audit` reviews on the resulting changed queue files and resolve findings through root step 10's fix/re-review rule. Then refresh root step 9's final-tree ledger and changed-file manifest: verify every changed plan link, score, sorted row position, dependency/file-group reference, claim-mirror transition, and required file deletion/retention. Queue cleanup must be reviewed and verified after its last content mutation before it is committed or landed.

- **After execution completes**: the user approved in Step 9 and C++ Code Change Process steps 2–11 completed with residuals routed. Remove the plan's row and file, prune every target reference from `## Dependencies` and `## File Groups`, and delete entries that then name fewer than two live plans. Include all cleanup in the reviewed and final-tree-verified session change set, then invoke `/finalize-changes` with the plan claim locator and `$Owner` so it releases that claim after verifying the landed commit; the clean landed worktree and branch remain registered for user-managed cleanup.
- **Terminal abandonment**: Step 3 classified the plan Obsolete, or the user chose "abandon and clean up" in Step 3/4 — remove the row/file, perform the same dependency/file-group pruning and review, then invoke `/finalize-changes` with the plan claim locator and owner.

If the run ends any other way, do **not** delete the row or plan file. On rejection in Step 9, remove this session's `[CLAIMED <date>] ` mirror, keep the refreshed plan file, review and final-tree-verify the queued-state changes, then invoke `/finalize-changes` with the plan claim locator and owner. If landing or owner verification fails, retain the plan claim and session worktree/branch and report it. On an error or user stop mid-execution, retain both claim and mirror and report the metadata.

### Step 9. Audit and grill the plan, present it, and request approval

The audit and grill run **before** approval so that once the user approves, implementation begins immediately with no further review or interview round-trip. Do the three sub-steps in order; all of Step 9 stays under one `Step 9` label so existing cross-references (the Step 8 contract, the edge cases) stay valid.

#### 9a. Audit and grill the plan (pre-approval)

Have one Fable subagent invoke `/plan-audit` on the plan file Step 7 wrote — this is C++ Code Change Process step 1, pulled ahead of approval. Validate its findings and carry accepted flaws and improvements into the grill; the audit does not edit the plan.

Invoke `/external-grill-plan` directly on the plan file Step 7 wrote, passing the accepted audit findings — this completes C++ Code Change Process step 1 before approval. **Do not print the plan's `## Summary` or the full plan before the grill.** The grill presents only concrete decisions, ambiguities, recommendations, or its required closing question; it silently updates the plan file with the resolved answers, so the plan the user sees in 9b is already refined.

- On a trivial/mechanical plan the grill commonly finds no decision points and returns with nothing to ask — expected; proceed straight to 9b.
- **Do not stop or summarise when the grill returns** — continue to 9b in the same turn. The only legitimate reasons to pause here are (a) the grill asked the user a question that is still open, or (b) the grill recommended running `/external-design-interface` first per its role-boundary clause; resolve those before presenting.

#### 9b. Present the final (grill-refined) plan

**The plan and its rationale MUST land in the session context window before approval is requested — the user must be able to scroll up and read the full plan text in the transcript while (and after) deciding.** Text sandwiched between tool calls, or emitted in the same assistant message as a tool call, is not reliably rendered to the user; an `AskUserQuestion` option `preview` pane is not scrollback and does not satisfy this requirement on its own.

Output the Step 7 markdown (as refined by 9a) as the **final text of the turn, with no tool calls in that message and none after it**, so it renders fully in the session window. End the turn there.

#### 9c. Request approval

When the user responds, first inspect the response itself. If it contains an unambiguous decision ("approved", "execute", "go ahead", "reject", "skip the TextureCache one"), honor it directly. **Approval is complete at that point: never call `AskUserQuestion`, re-present the plan, summarize it again, or ask for confirmation.** Proceed immediately to implementation or rejection handling in the same turn.

Only when the response is a neutral acknowledgement with no decision should `AskUserQuestion` request approval. Optionally duplicate the plan in the `Approve` option's `preview` as a convenience copy, but never as the only copy.

The `AskUserQuestion` is a single question along the lines of:

- **question**: "Approve this plan and proceed with implementation?"
- **options**: `Approve` (proceed to implementation), `Reject` (the refreshed plan and mirror removal are reviewed and passed to `/finalize-changes`; the plan returns to the queue)

If the `## Additional candidate locations` section contains any **Surfaced** entries (only those the Step 6 extension-review gate flagged as carrying new invariant exposure, needing a design decision, or unconfirmable — everything else was auto-folded), ask the user whether to fold them into the execution scope, defer them to a follow-up plan, or ignore them. Use a separate `AskUserQuestion` call (or a multi-select question) so the approval decision and the scope-expansion decision are tracked independently. Do **not** ask about auto-folded candidates — those are already in `## Execution steps` by design; just mention them in the presentation so the user can veto if they disagree, but don't gate on it. Because the review gate now clears the routine cases automatically, the Surface set is usually empty — when it is (or when every candidate was auto-folded), skip the scope-expansion question entirely.

If the user picks `Approve`, follow the standard C++ Code Change Process defined in the top-level `AGENTS.md`, **starting from step 2 (implementation)** — step 1 (audit and grill) already ran in 9a. Carry the user's decisions on additional candidates into the process and proceed straight into the edit in the same turn. After step 11, execute Step 8's coordinated queue cleanup and invoke `/finalize-changes` with the plan claim locator and owner.

If the user picks `Reject`, follow Step 8's rejection route: strip this session's `[CLAIMED <date>] ` mirror, keep the refined plan file and row queued, review the changes, then invoke `/finalize-changes` with the plan claim locator and owner. If landing fails, retain the claim and session worktree/branch and report it.

## Edge cases

- **No orphans found in Step 0**: skip the subagent dispatch entirely and proceed to Step 1. No mutations.
- **Step 0 subagent flags every orphan as stale**: no rows inserted; report the stale list to the user and continue to Step 1 against the unchanged table.
- **Orphan file lives under a subdirectory the table doesn't yet reference** (e.g., a new `Audio/` area): the row goes in at score-correct position regardless of subdirectory — the table is sorted by Score, not grouped by area.
- **Orphan is itself a Reference / Index document** that wasn't added to the `### Reference / Index Documents` subsection: Step 0b only excludes paths already listed there, so a genuinely-meta doc would be misclassified as a plan. The Opus subagent should detect this from the document's content (no execution steps, no `## Critical files` section, narrative overview tone) and label it `stale` with a justification recommending the user move it to the reference subsection.
- **Empty table** (`## Plans` table has no rows): report "Order.md has no plans" and stop. No mutations.
- **Top row is marked subsumed or index/meta in Dependencies**: Step 1c filters it; fall through to the next row. No claim happens for filtered rows — Step 2 only fires on the eventual selected target.
- **Plan file missing from disk** but row still in `## Plans`: the row and file are removed together at completion, so this state means the file was likely hand-deleted without cleaning up Order.md. Report the bookkeeping anomaly, ask the user whether to remove the stale row, and fall through to the next candidate. Do not claim a candidate whose plan file is missing — Step 3 would have nothing to read.
- **Claim already exists**: another session owns the plan. Automatic selection skips it. An explicit request reports `AgentCli lock status` metadata and requires user-approved conditional takeover; age is warning-only.
- **Marker/claim mismatch**: any `[CLAIMED ...]` marker without an AgentCli claim is stale informational state and does not block selection; a claim without a marker still blocks. Repair only the current session's mirror or an explicitly user-approved takeover.
- **Row or plan file disappears mid-run**: isolated worktrees do not receive remote checkout mutations. Treat this as a local mutation and inspect local edits/action history. If it appears only after reconciliation, verify the rebased primary commit actually completed the plan before restarting selection; otherwise stop and report rather than attributing it to another session.
- **User provided a plan name as an argument**: Step 1b handles this — normalize and use as the candidate; the dependency walk still runs from it downward.
- **Prerequisite missing from both the table and disk**: Step 1e already treats this as satisfied. No extra handling needed.
- **User rejects in Step 9 approval**: follow Step 8's rejection route — remove this session's mirror, keep the refreshed plan file and row, review the changes, then invoke `/finalize-changes` with the plan claim locator and owner. Retain the claim and session worktree/branch if landing fails.
- **Step 6 sweep finds an obviously-superseding plan**: if the codebase sweep finds that the plan is one instance of a much larger pattern that has its own existing plan in `Order.md`, surface that to the user in Step 9 so they can decide whether to abandon the current plan in favor of the broader one.

## What this skill does not do

- Does not execute the plan — that happens after Step 9 approval. Step 9 audits and grills the plan first (Step 9a — C++ Code Change Process step 1, pulled ahead of approval), so an approval proceeds into the process at step 2 (implementation).
- Does not reassess or re-prioritize the `## Plans` table. Its scoring fields are estimates used only to order the queue.
- Does not remove the Order.md row or delete the plan file at selection time. Selection creates the authoritative AgentCli plan claim and an informational row mirror; cleanup and owner-checked release follow Step 8.
- Does not blindly expand scope, but **does auto-fold aggressively**: every Step 6 candidate that the dedicated extension-review subagent clears (confirmed same transformation, mechanical, no new undeclared invariant exposure) is folded into `## Execution steps` with no user ask, tagged `[auto-folded sibling]` and listed **Folded** under `## Additional candidate locations` so it stays visible and vetoable. This includes **Related** and lower-confidence siblings — the thorough review gate, not a reflexive user prompt, is what vets them. Only candidates the review gate flags (new invariant exposure, a needed design decision, or unconfirmable sameness) are surfaced for an explicit Step 9 decision.
