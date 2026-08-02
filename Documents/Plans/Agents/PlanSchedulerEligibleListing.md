<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-02T01:12:02.004Z","dependsOn":[]} -->
# Plan Scheduler Eligible Listing

## Context

The scheduler's only surface is `plan validate|claim-next|claim-status|unclaim|complete|reject` (`Tools/WorktreeCli/AGENTS.md`); nothing lists eligible plans or live claims across sessions, and `claim-next` selects strictly by `(createdUtc, normalized path)` with no eligibility preview. Every "pick a Tier-N plan" request therefore improvises:

- Codex session `019fbeb5-453b` (landing `c7a85890`): the manager hand-wrote four enumeration scripts over ~210 session branches, pulling a 41,004-char `git worktree list --porcelain` dump plus 76,899 chars of module/AGENTS source into its context, and still selected wrong on the first pass, ending a turn with nothing claimed and a 13.4-minute user correction round-trip.
- Codex session `019fbe73-7e74` (landing `e93f52fa`): the user asked for a Tier-1 plan; bare `claim-next` claimed the Tier-3 `ReducePipelineManager.md`, costing a full abandoned prepare→defer→reclaim cycle (~2.4 M tokens) and a 49m53s user round-trip.
- Claude session `2180aa65` (landing `a7280bc9`): selection ran `head -20` over every Plan file — a measured 48.5 KB payload the host truncated — before a one-line-per-plan metadata pass answered the question.

Root cause: selection inputs (eligibility, claim state, creation order) exist only inside WorktreeCli's validator, so agents re-derive them from raw Git at high cost and low reliability.

## Design

Add a read-only WorktreeCli verb `plan list` that emits the validator's existing knowledge as one JSON object: schema version, and one row per executable plan with `path`, `createdUtc`, `dependsOn`, `state` (`eligible`, `blocked` with blocking paths, or `quarantined` with the diagnostic), and the live claim when present (`session`, `worktree`, `expiresUtc`). The verb reuses the validation pass `plan validate` already performs and takes the same `--repo`/`--worktree` arguments; it changes no state and takes no scheduler lock beyond what validation takes today.

Update `/next-plan` selection guidance: run `plan list`; for a tier-constrained request, read only the top eligible candidates' `Risk tier` prose lines in creation order until one matches, then issue the targeted claim. No tier field is added to the metadata marker — tier remains plan prose, and the marker stays immutable.

## Critical files

- `Tools/WorktreeCli/PlanSchedulerCommands.cpp` (or the file owning the `plan` verb dispatch) — the new verb over the existing validation model.
- `Tools/WorktreeCli/AGENTS.md` — the scheduler-surface list.
- `.agents/skills/next-plan/SKILL.md` — the selection step naming `plan list`.
- `Documents/Plans/AGENTS.md` — the scheduler-surface sentence, if it enumerates verbs.

## In scope

- The read-only `plan list` verb, its JSON contract, and its wiring into the existing `plan` command dispatch and validation model.
- The scheduler-surface documentation sentences and the `/next-plan` selection step.

## Out of scope

- Any change to claim, selection, completion, or rejection semantics; `claim-next` ordering stays `(createdUtc, normalized path)`.
- Any metadata-marker change, including a tier key.
- Machine-local claim storage format and healing rules.
- The `/next-plan` wording fixes owned by `Documents/Plans/Agents/NextPlanSelectionContract.md`.

## Risk tier and invariants

Tier 2 — a read-only verb on the coordination tool plus documentation; it blocks no session and changes no scheduler state. WorktreeCli remains the only component that parses scheduler state — this verb extends that single parser rather than adding a second one.

Invariants: `plan list` mutates nothing; its eligibility and claim rows agree with what `claim-next` would do at the same tree state; existing verbs' contracts are byte-unchanged.

## Acceptance criteria

- `plan list` over the current repository emits valid schema-versioned JSON whose eligible set and ordering match `claim-next`'s selection order, verified by comparing against a `claim-next`/`unclaim` probe in a scratch state or by inspection of the shared selection code path.
- Quarantined and dependency-blocked fixtures are reported with their reasons.
- WorktreeCli builds via `/compile`; `plan validate` still reports `status: valid` over the tree.

## Notes

This is agent-workflow tooling debt — it restores a capability the workflow already needs (deterministic selection input) rather than adding an engine capability, which is why it is a Plan and not a Feature.

## Coordination

`Documents/Plans/Agents/NextPlanSelectionContract.md` edits other sentences of `.agents/skills/next-plan/SKILL.md` (targeted-claim `none-available` meaning, deferral override, one-script-per-call). Whichever lands second merges its selection-step wording with the other's edits rather than restating them.
