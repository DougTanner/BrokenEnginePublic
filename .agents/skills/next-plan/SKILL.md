---
name: next-plan
description: Validates the live primary plan queue through AgentCli, lands any legacy orphan repair before restarting fresh, then atomically claims the highest-priority eligible plan (or an explicit plan), validates it against current code, refreshes stale details, audits and grills it, and presents a ready-to-execute plan for approval. Use when the user invokes `/next-plan`.
disable-model-invocation: true
argument-hint: "[plan-file-path]"
allowed-tools: [Read, Write, Grep, Glob, Agent, Edit, PowerShell, AskUserQuestion]
---

# Next Plan

Validates the clean primary queue and atomically claims its highest-priority eligible plan, then verifies that plan still describes a real problem in current code, refreshes stale references, scans for required propagation, audits and grills it up front, and presents a ready-to-execute plan for explicit user approval.

All requirements are outcome-first. Delegate with a self-contained fresh Claude prompt or Codex `fork_turns:"none"`; edit/search/run commands through Claude's local tools or Codex's local shell/edit tools; present the complete plan as ordinary transcript text; request approval through Claude `AskUserQuestion`, Codex `request_user_input`, or a direct blocking question when that UI is unavailable.

For the delegated Step 0 orphan evaluation, Step 4 research/audit, conditional Step 6 sweep, and Step 9 plan audit, follow the shared [`be-agent-report/v1`](../../references/subagent-reporting.md) contract. Assign each subagent a unique absolute report path under this session worktree's `Temp/AgentReports/`, require it to write the full report there and return only the compact indexed envelope. For every consumed report, require `REPORT`, `REPORT_SHA256`, the requested indexed IDs, their exact evidence locators, and dependencies; invoke `Read-AgentReportSection.ps1` once per exact range under the shared contract before adjudicating or synthesizing. Delegated report paths and source-scope packet paths are transient coordination metadata: never copy one into the canonical plan.

## Preconditions

- The skill assumes **bypass-permissions** mode and mutates without further confirmation only at these points: legacy orphan repair through AgentCli `add` followed by its own verified landing; AgentCli `claim-next`; Step 7's synthesized plan-file replacement; and Step 8's AgentCli `complete` on an authorized terminal-cleanup route. User approval is requested only on the final synthesized plan.
- The skill does **not** require plan mode. It preserves the existing audit-grill-present-approve contract in Step 9; an unambiguous approval completes the root Approve and classify stage and proceeds directly to Implement and propagate.
- **`/next-plan` requires a wrapper-created isolated worktree, its live AgentCli session claim, and the wrapper's authoritative primary checkout and branch.** The session must start clean at the same commit as primary. If any identity is missing or ambiguous, stop; never create or adopt a worktree here.
- Set `$AgentCli` to `$ROOT\Tools\AgentCli\Platforms\VisualStudio2026\Output\AgentCli.exe`. If it is missing, stop and report that explicitly authorized primary maintenance through `/compile` is required. Resolve `git rev-parse --git-common-dir` to a canonical absolute path, take `$Primary` and `$Branch` from wrapper provenance, generate one `$Owner` with `lock token`, and retain it for the selected row's full lifecycle.
- AgentCli exclusively parses and mutates executable `Order.md` rows, validates the cross-queue dependency graph, selects eligible plans, and acquires/releases queue locks. `/next-plan` never parses or edits either `Order.md`, never interprets prose prerequisites, and never persists claim metadata in repository files.

## Workflow

Execute these steps in order. Step 0 either proves the primary queue valid or lands a standalone legacy repair and ends this session. Steps 1–2 validate and atomically claim from live primary. Step 7 replaces only the selected plan's prose. Step 8 defines terminal completion or retained-queue release. Everything else is research and synthesis; audit, grill, and approval remain in Step 9.

### Step 0. Validate primary and repair legacy orphans before selection

Run:

```text
plan order validate --repo <common-dir> --worktree <primary-worktree>
```

Require exit `0` and JSON `ok: true` before selection. Stable diagnostics from this command are authoritative for malformed rows, missing files, executable/reference overlap, invalid dependencies/cycles, and unindexed executable files. Do not reproduce or weaken those checks in the skill.

If primary validation reports a legacy orphan, selection is blocked. Dispatch the existing Opus orphan-evaluation role once for all reported orphan paths under the shared reporting contract. It reads each plan and spot-checks current source, then returns `tier`, `effort`, `impact`, `risks`, `notes`, explicit already-live `dependsOn`, prerequisite-first grouping, and scoring justification. Main adjudicates semantic duplicates and stale/non-executable cases; any deletion or reference reclassification needs the user's explicit decision because AgentCli `add` only adopts executable plans.

For every approved executable adoption, create a schema-version `1` `operation: "add"` request beneath this session worktree's `Temp/`, grouping prerequisite-first sequences and independent plans separately, then run:

```text
plan order add --repo <common-dir> --worktree <session-worktree> --owner <token> --session <label> --request <Temp repo-relative JSON>
```

Do not edit `Order.md`. Require the add receipt and a passing session `plan order validate`. Treat the repair as a standalone queue-only change: create its Tier-1 execution-control record, run only the triggered targeted check, workflow-coherence review, `/verify-changes`, and conditional-audit/finalization roles, land it into primary, and end this session without claiming a plan. Queue prose alone does not trigger paired correctness or session audits. The user must invoke `/next-plan` from a fresh wrapper-created session so `claim-next` can prove a clean primary/session baseline. A failed repair stays as explicit retryable session state and blocks selection.

### Step 1. Validate the clean session snapshot

When Step 0 passed without mutation, run `plan order validate --repo <common-dir> --worktree <session-worktree>` and require the same passing result. The session must remain clean and byte-identical to the primary commit; otherwise stop with the exact stale-session or validation diagnostics. Do not select from a dirty or locally repaired snapshot.

If the user supplied `$ARGUMENTS`, normalize it to one canonical repository-relative identity under `Documents/Plans/` and pass it as `--plan`. Reject rooted paths, raw `..`, or a Features identity; this skill selects only the Plans queue. With no argument, omit `--plan` and let AgentCli choose the first unclaimed row whose structured dependency rows are absent.

### Step 2. Atomically validate, select, and claim

Run exactly one selection transaction:

```text
plan order claim-next --repo <common-dir> --primary-worktree <primary-worktree> --worktree <session-worktree> --branch <target-branch> --owner <token> --session <label> --queue plans [--plan <Documents/Plans/...>]
```

AgentCli acquires both queue locks in canonical order, revalidates live primary, proves primary/session commit and selected-plan byte equality, applies structured dependency blocking, creates the global row claim, and releases the locks without repository edits. Require `claimed: true`, successful unlock results, the normalized plan path, row fields, dependency disposition, primary commit, Order/plan hashes, and matching claim metadata before continuing. Retain the returned receipt and `$Owner` through Step 8 and finalization.

For automatic selection, `no-eligible-row` means no plan is currently selectable; report the returned blockers and stop. For explicit selection, report exact dependency/claim/missing blockers. Existing claim takeover remains user-authorized only: inspect owner metadata with `plan row status`, ask explicitly, and use owner-matched `plan row steal --expect <reported-owner>` only after a fresh primary/session validation. A stale-session, plan-byte mismatch, validation failure, or unlock failure blocks; never fall back to manual row parsing, queue locking, or the removed sidecar.

Do **not** edit `Order.md` or the plan file to record the claim, and do not remove either at selection time. If the local row/file later vanishes, inspect local action history; only primary history proving another completed landing can authorize redundant-claim release.

### Step 3. Relevance check — does the code still exist?

Read the target plan file in full. Extract every file path, function name, class name, and cited line number mentioned in the plan.

For each referenced location, verify it still exists:

- **File paths**: Glob or Read to confirm the file is present.
- **Symbols** (functions, classes, members, constants): Grep for the exact identifier. If the plan cites line numbers, Read that region and confirm the symbol is on (or very near) the cited line.
- **Conditions** the plan depends on (e.g., "the RNG is wall-clock-seeded in `ResetState`"): Grep to confirm the condition still holds. A plan that fixes a bug already fixed upstream is no longer relevant.

Classify the plan into one of four buckets:

- **Fully relevant** — every reference resolves; plan proceeds as-is.
- **Partially relevant** — some references moved, got renamed, or shifted by a few lines; plan proceeds with refreshed references (Step 5).
- **Obsolete** — the bug is already fixed, the file was deleted, or the code was rewritten in a way that invalidates the plan's premise. Show the evidence and ask the user to approve Step 8 terminal cleanup or defer the unchanged plan; obsolescence does not itself authorize `complete`.
- **Ambiguous** — the original intent is unclear given current code. Ask the user before proceeding. If the user decides to abandon the plan, ask whether to clean it up or defer it back to the queue; both routes use Step 8's owner-checked row unclaim, and only cleanup removes the row/file.

### Step 4. Validity check — is it still worth doing?

Boundary with Step 3: **Step 3 asks "does the code still exist"; Step 4 asks "does the problem still exist."** The Obsolete bucket in Step 3 catches cases where the cited code was deleted or the fix already landed. Step 4 goes further and questions whether the plan's premise is still valuable even when the code is intact.

Relevance is necessary but not sufficient. A plan can still describe real code yet no longer be worth the effort. Evaluate:

- Does the plan address a real problem (correctness bug, determinism hazard, measurable perf, debt blocking other work) or a cosmetic preference that may no longer matter?
- Has the surrounding subsystem been refactored in a way that made the concern moot (e.g., the hot path the plan optimizes is no longer hot)?

Treat the Order.md Tier / Effort / Impact / Risks / Score fields as fixed estimates used only to order the queue. Do not reassess or re-score them while selecting or executing a plan.

**"Worth doing" rubric** — the plan clears the bar if ANY of:

- it fixes a correctness/determinism hazard still present in current code;
- it delivers measurable perf on a path still hot (check against the most recent capture/baseline the plan or its Notes cite);
- it unblocks a named queued plan or feature (the structured dependency graph returned by AgentCli, or an explicit statement in the plan body);
- it deletes a real abstraction or at least ~1,000 `bt-token-v1` estimated tokens of implementation. Run [`../../scripts/Measure-Tokens.ps1`](../../scripts/Measure-Tokens.ps1) on the implementation the plan actually removes rather than whole unrelated files; the metric is normalized UTF-8 bytes divided by four and rounded up, not an exact model-token count.

It fails the bar when the concern is now merely cosmetic or modest, no queued plan depends on it, and the work has materially expanded; it also fails when another queued plan supersedes or will rewrite the same code — surface that plan instead.

If the plan no longer clears the bar, stop and ask the user. The choice is between "abandon" (Step 8 lands row/file cleanup, then owner-unclaims the row) or "defer it to the queue" (owner-unclaim it, keep repository files, and freeze this worktree until a fresh reclaim). Surface both options.

**Conditional decision-grade evidence gate:** trigger this gate only when the plan's worth or approach depends on an unresolved performance measurement, root-cause claim, external API/library behavior, or architectural assumption that direct repository inspection cannot settle. Skip it for mechanical refreshes, trivial refactors, and plans whose cited evidence already decides the rubric; record `Research evidence: none` and continue without a research subagent.

When triggered, the main agent frames one explicit research question, the decision that answer will support, and concrete success criteria. The main agent never performs the actual research inline. Delegate it to a fresh Claude prompt or Codex `fork_turns:"none"` subagent using the shared [`be-agent-report/v1`](../../references/subagent-reporting.md) contract:

- Use a Sonnet/Luna evidence gatherer for direct code, history, benchmark, or web evidence, or invoke an existing specialist skill when one matches the evidence domain. Require primary evidence as direct `path:line` citations, verbatim diagnostic lines, or authoritative links rather than an unsupported summary.
- Parallelize only independent evidence domains. Keep dependent questions in one chain so later work consumes the earlier report instead of rediscovering its facts.

Each delegate writes the full report and returns only the compact indexed envelope. Read the indexed decision-driving sections needed for adjudication, not the report's bulk search trace, then decide whether the plan is worth doing in the main context. Record the decision-grade evidence and citations in Step 7's `Research evidence` field, but never record a transient report path there.

For every Architectural-tier plan, dispatch the existing Opus/Terra second-opinion audit under its own unique report path. When the evidence gate ran, give it the framed question, decision, success criteria, and evidence-report path so it challenges the implications without repeating research. When the gate was skipped, give it the plan and current-code scope directly. Read every indexed decision-driving finding before the main adjudicates the worth-doing decision.

### Step 5. Refresh the plan for current code state

Before changing or synthesizing plan text, write a unique immutable source-scope packet under this worktree's `Temp/AgentReports/`; require the allocated path to be absent, create it once, then read it back. The packet is coordination evidence, not a `be-agent-report/v1` subagent report. It contains:

- canonical plan path, fixed session-start commit, exact original bytes encoded losslessly plus decoded text, and the SHA-256 of those bytes;
- `Tracked source: <author-commit>:<plan-path>@<blob-id>` only when the most recent commit at or before the fixed baseline that changed the plan path introduced the captured blob and that commit's path/blob identity matches; otherwise `Tracked source: unavailable — <untracked, blob mismatch, or uncertain path history>`;
- stable `S###` IDs, in source order, for every executable source-plan requirement, each paired with its verbatim source excerpt or unambiguous captured anchor.

That path-changing revision is the plan's author baseline for drift evaluation. Do not use a later commit merely because its tree still contains the same bytes, and do not invent a moving author date from the session baseline. An untracked, mismatched, or uncertain path/blob identity leaves the author baseline unavailable; it is not evidence of no drift and triggers the conservative sweep in Step 6. Verify the read-back bytes against the recorded SHA-256 before continuing, never mutate the packet, pass it to Step 9's `/plan-audit`, and never write its transient path into the canonical plan.

For Fully or Partially relevant plans, Step 5 may refresh line citations, renamed symbols, moved paths, and equivalent wording. It may add only correctness-required propagation that passes the `P###` rule below. Do not silently adjust the approach for an additional caller, branch, state interaction, shared helper, prerequisite, failure mode, subsystem, invariant boundary, or verification obligation. Record every such material discovery as a `C###` candidate and route it through the same state machine used for Step 6 findings.

If the source contains `## Coordination`, preserve that section verbatim in synthesis except for approved refreshes of current paths or symbol names. Do not reinterpret, summarize, weaken, or move its reciprocal mandatory constraints into Context. If absent, do not invent a section from ordinary overlap observations.

### Step 6. Search the codebase for similar changes the plan may have missed

After Step 5, evaluate whether a sibling sweep is required. Trigger it only when at least one condition holds:

- the captured source actually claims an exhaustive transformation or mirrored family (`all`, `every`, or an equivalent bounded family claim);
- an execution step changes a public or cross-translation-unit signature;
- an execution step changes a shared, serialized, CRC-participating, or collection data member;
- the source explicitly says its search/enumeration is incomplete;
- code after the author baseline materially changed a cited symbol, its callers, or an invariant boundary relevant to the source transformation;
- the author baseline is missing or uncertain, which conservatively records `Sibling sweep: triggered — author baseline unavailable`.

Record the evaluated author baseline, cited paths/symbols/invariant boundaries, and exact trigger reason in synthesized Context. If none applies, record `Sibling sweep: not triggered — <reason>` and dispatch zero sweep or extension-review agents.

When triggered, dispatch exactly one fresh Sonnet code-search agent. Apply [`be-agent-report/v1`](../../references/subagent-reporting.md), give it the verified source packet, current plan text, explicit transformation pattern, author baseline and drift boundary, and relevant subsystem instructions. Ask it to search oversights and post-authoring drift and report every candidate with `path:line`, evidence, Oversight/Drift, Identical/Related, confidence, and exact proposed edit. There is no second extension-review dispatch. The main session reads every indexed candidate and validates it against current source and the captured scope.

Assign stable `C###` IDs in discovery order to all material Step 5 and Step 6 candidates and preserve every candidate in Step 7's `## Additional candidate locations`. Route each through exactly one authority state:

- **Folded required propagation `P###`** — execution authority derived from named `S###` and `C###`. This is allowed only when current evidence proves the exact edit, with high confidence, is required for compilation or correctness to realize that named `S###`. A `P###` may introduce intermediate caller signature/result handling and the exact visible behavior already explicitly authorized by `S###` when necessary to realize it, but it may introduce no contract, behavior, failure mode, subsystem, invariant, format, build mode, client/server affinity, or verification dimension beyond `S###`. State the exact edit. Related or lower-confidence candidates never qualify.
- **Delta requested `C###`** — pending, non-execution, and blocking until Step 9's grill records an exact user disposition.
- **Approved delta `D###`** — execution authority derived from named `C###` and the exact user decision; add it only after approval, in both the approved-delta ledger and execution steps.
- **Follow-up pending `C###`** — non-execution. Index it as a residual for the conditional `/create-follow-up-plans` role.
- **Rejected false positive `C###`** or **Rejected by user `C###`** — non-execution, with repository or decision evidence.

Every execution step must cite exactly one `S###`, `P###`, or `D###`; a `C###` alone never grants execution authority. Candidate correctness, similarity, or low implementation cost does not waive this contract.

For workflow verification only, use the exact dry-run cases and expected routing in [`references/scope-routing-fixtures.md`](references/scope-routing-fixtures.md). Do not load that reference during ordinary plan selection.

If a triggered sweep returns more than ~10 candidates, the plan likely describes a pattern broad enough to warrant a dedicated systematic sweep rather than a one-off fix. Note that observation in the synthesized plan and surface it to the user in Step 9 — do not bury dozens of candidates under an unrelated plan.

### Step 7. Synthesize the final actionable plan

Assemble a single markdown document matching the template below, then `Write` it over the source plan file — the refreshed plan replaces the original as the canonical record: `/external-grill-plan` updates this file with its resolved answers, the implementation subagent reads it, and it stays recoverable if the run is rejected. This is also the artifact the user will be asked to approve in Step 9.

For the title, use the target plan file's top-level `# ` heading if one exists. Some plans are plain-text `.txt` files with no H1 — for those, fall back to the selected plan identity's filename stem and **preserve the original casing** (don't re-PascalCase kebab-case or vice-versa).

Examples:
- `Audio/GateVoiceLifecycleDuringReplay.txt` (no H1) → `GateVoiceLifecycleDuringReplay`
- `Graphics/ocean-phase-1-pbr-foundation.md` (no H1) → `ocean-phase-1-pbr-foundation`
- `Network/Architecture_FleetRngDeterminism.md` (has `# Fleet RNG Determinism` at top) → `Fleet RNG Determinism`

```
# <Plan title — H1 from plan file, or selected plan filename stem>

## Summary
**What this plan does:** <2-4 sentences in plain prose describing the change in concrete terms. Name the subsystems / files touched and the user- or engine-visible behavior change. Avoid restating the title; avoid step-by-step detail (that lives in Execution steps).>

**Why it's good for the codebase:** <2-4 sentences naming the concrete benefit. Pick from: correctness bug fixed, determinism hazard closed, measurable perf win, debt removed that unblocks <named follow-up>, simplification that deletes <N> bt-token-v1 / removes <named abstraction>, hot-path allocation eliminated, etc. Be specific — "improves code quality" is not acceptable; "removes the per-frame heap allocation in `BlasterPostRender::Update` flagged by allocation tracking" is.>

## Context
- Source: <normalized plan identity from the claim receipt; removed by AgentCli complete after successful execution>
- Queue row: Tier <T> / Effort <E> / Impact <I> / Risks <R> / Score <S>
- Notes: <the claim receipt's Notes field, verbatim>
- Relevance: <Fully | Partially> — <one-line justification>
- Dependency disposition: <the claim receipt's structured dependency disposition>
- Coordination warnings: <"none" or each warning-only overlap with other plan/session, intersecting files, and likely landing order>
- Research evidence: <the Step 4 decision, concise decision-grade evidence with direct `path:line` citations / diagnostic lines / authoritative links, and whether it cleared the success criteria; never include a `Temp/AgentReports/` path>
- Current-source refinements: <bullet list of citations or scope facts established in Step 5, or "none">
- Sibling sweep: <`not triggered — reason` or `triggered — reason`; include the author baseline or `unavailable` and the evaluated cited symbols/callers/invariant boundaries>

## Coordination
<Copy the source plan's section verbatim except approved current path/symbol refreshes; omit this heading when the source had no Coordination section.>

## Provenance map
- S001 — Source step — <captured source requirement; repeat for every source execution requirement>
- P001 — Required propagation derived from S001 and C001 — <exact edit and why every fold condition holds; omit when none>
- D001 — Approved delta derived from C002 — <exact approved execution; omit when none>

## Approved-delta ledger
<Write exactly one mutually exclusive form: literal `- none` when empty, or one `- D### — derived from C### — <exact user decision and resulting execution authority>` row per approved delta; never both.>

## Execution steps
1. [S001] <Refreshed source implementation step, with current `path:line` citations.>
2. [P001] <Exact correctness-required propagation, if any.>
3. [D001] <Exact user-approved delta, if any.>
...

## Out of scope
<The source plan's existing Out of scope content, refreshed only for current path/symbol citations. If absent, write "Not specified in source plan.">

## Acceptance criteria
<The source plan's existing Acceptance criteria content, refreshed only for current path/symbol citations. If absent, write "Not specified in source plan.">

## Additional candidate locations
<Every material Step 5/6 candidate, one bullet per C###, with `path:line`, Oversight/Drift, Identical/Related, confidence, exact edit, evidence, and one authority state: `Folded required propagation P### derived from S###`; `Delta requested`; `Approved delta D###`; `Follow-up pending`; `Rejected false positive`; or `Rejected by user`. If no candidate exists, write "No additional candidates found." If there are more than ~10, note that a dedicated systematic plan is likely.>
```

Authoring the **Summary** section is mandatory and must come from synthesis, not boilerplate. Source the **What** from the plan's body (its goal statement, top-level description, or the union of its execution steps if no narrative exists) and the **Why** from a combination of the plan file's stated rationale and the claim receipt's `Impact` / `Notes` fields. If the plan file contains no rationale at all, infer the Why from the code drift uncovered during Steps 3-5 and prefix the sentence with "Inferred:" so the user knows it isn't author-supplied. Never write a generic Why like "improves quality" or "cleans up the codebase" — if you cannot name a concrete benefit, surface that gap to the user in Step 9 instead of papering over it.

Keep source execution steps in their original order with current citations and their stable `S###` IDs. Append only validated `P###` and user-approved `D###` steps. The provenance map, approved-delta ledger, execution list, and candidate dispositions must agree; `Delta requested`, `Follow-up pending`, and either Rejected state stay out of execution. Preserve the source `## Coordination` section under the rule above and carry its mandatory constraints through implementation and landing. Queue completion belongs to Step 8, not the execution list.

Preserve the source plan's `## Out of scope` and `## Acceptance criteria` sections during synthesis; do not add, remove, or broaden their scope except to refresh path/symbol citations and names. A candidate that conflicts with an explicit exclusion or criterion cannot be `P###`; keep the contradiction visible for Step 9 plan audit and grill.

### Step 8. Completion contract — AgentCli complete or retained queue state

Nothing is deleted at selection time. Use AgentCli `complete` only after successful execution with all residuals routed, or after the user explicitly approves obsolete/abandoned cleanup. Before successful-execution cleanup, carry every `Follow-up pending C###` as an indexed residual into the conditional `/create-follow-up-plans` role and require a mapping for every candidate.

On an authorized cleanup route, run:

```text
plan order complete --repo <common-dir> --worktree <session-worktree> --owner <claim-owner-token> --session <label> --plan <normalized selected plan>
```

Require the matching owned claim and a receipt bound to the resulting Plans and Features queue hashes. AgentCli transactionally removes the executable row and plan file and prunes every inbound structured dependency edge; never perform those Markdown/file edits yourself. The global row claim remains held. Because completion changes final-tree bytes, rerun the affected targeted checks, review only contract-significant changed regions not already seen, and regenerate `/verify-changes`; its final ledger must include the exact complete receipt and a passing session `plan order validate`. Queue cleanup does not by itself trigger paired or per-file-group session audits; apply the root conditional whole-change-audit triggers once during finalization. Invoke `/finalize-changes` with the selected plan identity, owner, and receipt so reconciliation can reapply completion and post-landing unclaim safely.

Rejection and deferral retain both row and plan file; never call `complete`. On rejection, review, verify, and land only the refreshed plan file, then let `/finalize-changes` owner-check and unclaim after that landing. On explicit deferral or another safe stop with no repository mutation requiring landing, use the existing owner-only `plan row unclaim` route, keep the partial worktree intact, and freeze it against further edits. A resume requires a clean/current session as enforced by fresh `claim-next`; if partial changes prevent that, reconcile them through the normal landing/abandonment decision first. Errors and user stops retain and report the claim unless an authorized landing or safe-stop unclaim has completed.

### Step 9. Audit and grill the plan, present it, and request approval

The audit and grill run **before** approval so that once the user approves, implementation begins immediately with no further review or interview round-trip. Do the three sub-steps in order; all of Step 9 stays under one `Step 9` label so existing cross-references (the Step 8 contract, the edge cases) stay valid.

#### 9a. Audit and grill the plan (pre-approval)

Have one Fable subagent invoke `/plan-audit` on the plan file Step 7 wrote as part of the root Approve and classify stage. Supply the verified immutable source packet, canonical provenance map, approved-delta ledger, explicit approved-delta summary, and draft execution-control record. Apply the shared [`be-agent-report/v1`](../../references/subagent-reporting.md) contract: assign a unique absolute report path, accept only the compact indexed envelope, and read every indexed finding section before validating it. Require its report/result to establish successful independent verification of the exact packet identity and source provenance; a missing, failed, mismatched, or unaudited packet blocks implementation authority. Carry accepted flaws and improvements into the grill; the audit does not edit the plan.

Invoke `/external-grill-plan` directly on the plan file Step 7 wrote, passing the accepted audit findings and draft execution-control record. This resolves plan decisions and classification before approval. The grill must resolve every `Delta requested C###`: record the exact user decision as `Approved delta D###`, `Follow-up pending C###`, or `Rejected by user C###`; only an approved delta enters the ledger, provenance map, and execution steps. Promote each follow-up to an indexed conditional-planning residual immediately. **Do not print the plan's `## Summary` or the full plan before the grill.** The grill presents only concrete decisions, ambiguities, recommendations, or its required closing question; it updates the plan file with resolved answers, so the plan the user sees in 9b is already refined.

- On a trivial/mechanical plan the grill commonly finds no decision points and returns with nothing to ask — expected; proceed straight to 9b.
- **Do not stop or summarise when the grill returns** — continue to 9b in the same turn. The only legitimate reasons to pause here are (a) the grill asked the user a question that is still open, or (b) the grill recommended running `/external-design-interface` first per its role-boundary clause; resolve those before presenting.

After the grill, perform a final authority-state check against the verified source packet: every execution step cites a matching `S###`, `P###`, or `D###`; every `D###` agrees with the exact user decision and approved-delta ledger; every candidate is retained; no `Delta requested` remains; and non-execution states do not appear in execution. Stop before presentation if any check fails. Materialize the approved-delta summary exactly from the final `D###` ledger (`none` when empty) and compare the exact final D ID set, complete ledger, and summary with the state identified in the successful audit result. If the grill created, removed, or changed any D authority, run a fresh authority-focused Fable `/plan-audit` on the complete final authority state under a new shared-contract report path and require success before presentation. If that state is unchanged, including an unchanged empty ledger, dispatch zero additional audits.

Prepare the manager execution-control record before presentation: fixed process
baseline; highest applicable Tier 1/2/3 and concrete triggers; required roles;
each conditional role and objective trigger; and the initial
`criterion -> decisive check -> expected result -> independent signal if
duplicate` matrix. Keep it as manager coordination state, not canonical plan
authority. The process applies only when the fixed baseline contains the landed
linear-process definition.

The downstream Review and resolve stage uses the record to select one applicable
first correctness reviewer, adding adversarial or shader review only when its
objective trigger applies. Main adjudicates the union of cited evidence once;
accepted fixes are focused and rerun only invalidated review/check evidence.

#### 9b. Present the final (grill-refined) plan

**The plan and its rationale MUST land in the session context window before approval is requested — the user must be able to scroll up and read the full plan text in the transcript while (and after) deciding.** Text sandwiched between tool calls, or emitted in the same assistant message as a tool call, is not reliably rendered to the user; an `AskUserQuestion` option `preview` pane is not scrollback and does not satisfy this requirement on its own.

Output the Step 7 markdown (as refined by 9a) as the **final text of the turn, with no tool calls in that message and none after it**, so it renders fully in the session window. End the turn there.

#### 9c. Request approval

When the user responds, first inspect the response itself. If it contains an unambiguous decision ("approved", "execute", "go ahead", "reject", "skip the TextureCache one"), honor it directly. **Approval is complete at that point: never call `AskUserQuestion`, re-present the plan, summarize it again, or ask for confirmation.** Proceed immediately to implementation or rejection handling in the same turn.

Only when the response is a neutral acknowledgement with no decision should `AskUserQuestion` request approval. Optionally duplicate the plan in the `Approve` option's `preview` as a convenience copy, but never as the only copy.

The `AskUserQuestion` is a single question along the lines of:

- **question**: "Approve this plan and proceed with implementation?"
- **options**: `Approve` (proceed to implementation), `Reject` (the refreshed plan is reviewed and landed, then the row is returned to the queue by owner-unclaim)

Candidate scope decisions already completed in 9a; do not reopen them during plan approval. If any `Delta requested` remains, return to the grill instead of presenting or requesting approval.

If the user picks `Approve`, bind the prepared execution-control record to the
approved plan, exact fixed baseline, and final approved-delta state; this
completes the root Approve and classify stage. Pass that exact record,
source-packet identity, final ledger/summary, and the successful audit result
covering the exact final D state to every downstream role. Follow the standard
C++ Code Change Process defined in the top-level `AGENTS.md` through the remaining
named stages: Implement and propagate; Run targeted pre-review checks; Review and
resolve correctness; Apply conditional hygiene; Verify the acceptance matrix; and
Reconcile, audit when triggered, and finalize. Carry the user's decisions on
additional candidates into the process and proceed straight into the edit in the
same turn. After all
triggered conditional-hygiene/follow-up roles complete, execute Step 8's
coordinated queue cleanup and invoke `/finalize-changes` with the plan row locator
and owner.

After approval, subagents treat this canonical plan as immutable. A discovered change to behavior, scope, acceptance criteria, architecture, or verification obligations returns an exact material plan delta without editing; main requests explicit approval, then records the candidate and a new `D###` in the provenance map, approved-delta ledger, execution steps, and approved-delta summary. Before implementation resumes, run a fresh authority-focused Fable `/plan-audit` under a new shared-contract report path and require its successful result to explicitly cover the complete final D ID set, ledger, and summary. Non-material corrections must not alter those dimensions.

If the user picks `Reject`, follow Step 8's rejection route: keep the row queued, review and land only the refined plan file, then invoke `/finalize-changes` with the plan identity and owner. If landing fails, retain the row claim and session worktree/branch and report it.

## Edge cases

- **No primary validation diagnostics**: skip orphan evaluation and proceed directly to session validation and `claim-next`.
- **Legacy orphan diagnostic**: selection stays blocked until the approved AgentCli add repair is verified and landed; end this session after landing and restart fresh.
- **Orphan is stale or really a reference/index document**: do not add it as executable. Obtain the user's exact cleanup/reclassification decision, verify and land that standalone repair, then restart fresh.
- **No executable rows**: `claim-next` returns `no-eligible-row`; report it and stop without a claim.
- **Missing plan file, malformed row, or dependency cycle**: primary `validate` reports the stable diagnostic before selection. Do not claim or manually repair the queue.
- **Claim already exists**: automatic `claim-next` skips it; explicit selection returns owner/blocker evidence and requires user-approved conditional takeover. Claim age is warning-only.
- **Repository text suggests ownership**: ignore it for coordination and remove it when the file is otherwise in scope; only the authoritative queue-lock snapshot and row status determine ownership.
- **Row or plan file disappears mid-run**: isolated worktrees do not receive remote checkout mutations. Treat this as a local mutation and inspect local edits/action history. If it appears only after reconciliation, verify the rebased primary commit actually completed the plan before restarting selection; otherwise stop and report rather than attributing it to another session.
- **User provided a plan name as an argument**: Step 1 normalizes it and passes the canonical identity to explicit `claim-next`; structured dependencies still block it while their rows exist.
- **User rejects in Step 9 approval**: follow Step 8's rejection route — keep the refreshed plan file and row, review and land only the refreshed plan, then post-land owner-unclaim. Retain the claim and session worktree/branch if landing fails.
- **Step 6 sweep finds an obviously-superseding plan**: if the codebase sweep finds that the plan is one instance of a much larger pattern owned by another queued plan, surface that to the user in Step 9 so they can approve abandonment cleanup or retain/defer this plan.

## What this skill does not do

- Does not execute the plan — that happens after Step 9 approval. Step 9 audits, grills, and classifies first, so approval proceeds into Implement and propagate.
- Does not reassess or re-prioritize queue rows. Their scoring fields are fixed estimates used only to order the queue.
- Does not remove the executable row or delete the plan file at selection time. Selection creates only authoritative AgentCli row coordination state; AgentCli completion and owner-checked unclaim follow Step 8.
- Does not treat similarity as authority. Only captured source steps, exact correctness-required propagation, and exact user-approved deltas may enter execution; every other candidate remains non-execution or becomes a conditional follow-up-plan residual.
