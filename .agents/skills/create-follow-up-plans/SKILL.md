---
name: create-follow-up-plans
description: Converts proven pre-existing or out-of-scope Change Workflow residuals into concise, evidence-backed follow-up plans under `Documents/Plans/<area>/`, then prepares the correct tracked edit or structured WorktreeCli add/update transaction. Do not route an in-scope acceptance failure out of the active change. Also use when asked to queue review findings without duplicating existing plans.
allowed-tools: [Read, Write, Edit, Glob, Grep, PowerShell]
---

# Create Follow-up Plans

Turn eligible residuals into executable debt plans. This skill owns candidate adjudication, grouping, duplicate detection, area and filename placement, collision handling, scoring, dependencies, and Coordination decisions; callers supply evidence, not those decisions.

## Inputs and boundary

Require direct finding evidence, originating stage and acceptance gap, affected symbols/files, prior reviewer or user decisions, the active intent/plan, related residuals, and the session changed-file list. Inspect missing facts; never invent evidence or behavior.

Read `Documents/AGENTS.md` and `Documents/Plans/AGENTS.md` completely. Their current plan shape, scoring, dependency, and Coordination rules override this skill. This skill creates debt plans only. Report a capability addition for main-agent routing to `Documents/Features/`; do not disguise it as debt.

Reject an in-scope acceptance failure, including required structural work: it remains a blocker in the active change. Also reject stale, disproven, fixed, stylistic-only, and evidence-free candidates, stating why.

Queue mutation requires the current checkout to be the wrapper session worktree named by `BROKEN_ENGINE_WORKTREE_PATH` and `BROKEN_ENGINE_WORKTREECLI_SESSION_WORKTREE`, with admission mode `session` and a nonempty `BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER`. Use that owner for both WorktreeCli `--owner` and `--session`; never mint another token or reconstruct wrapper identity. In an ordinary checkout, perform the evidence, duplicate, grouping, placement, tier, and scoring work, then report proposed plan contents and rows without writing plan files, requests, or queue state.

## Workflow

### 1. Prove and consolidate candidates

For every candidate:

1. State the false required condition and its originating criterion, accepted finding, or residual.
2. Confirm root cause and unresolved state from current source or direct logs. Record durable `path:line`, symbol, and observed behavior evidence.
3. Prove it is pre-existing or outside the approved implementation boundary.
4. Preserve user decisions and the authority order among user direction, approved plan, documentation, and current behavior; report contradictions.

Group items only when root cause, implementation boundary, invariant, and verification strategy all match. Split independently landable work, architectural decisions, separate subsystems, or materially different risks.

Run the provisioned `Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe plan order validate --repo <absolute git-common-dir> --worktree <checkout>` before duplicate decisions or any queue mutation. If the executable is absent, stop and report that authorized primary maintenance through `/compile` is required. Require exit `0` and JSON `ok: true`; stale-baseline `missing-plan-file` notices remain non-blocking. Treat its `rows` as the only executable inventory and source of `rowSha256`; never parse or edit machine-local rows.

Search all live plan files by symbols, paths, root-cause terms, outcome, and `## Coordination`. A plan is a duplicate when it owns the same root cause and implementation boundary. Map the candidate to it unless the proven acceptance gap requires extending that plan.

### 2. Draft and classify

Choose the existing owning area and a concise PascalCase filename; never overwrite a collision. Draft the smallest decision-complete plan with `# Title`, `## Context`, `## Design`, `## Critical files`, `## Out of scope`, `## Acceptance criteria` when the diff is insufficient, and `## Notes`. Include verified root cause, originating gap, implementation boundary, and applicable determinism/CRC, serialization/`.pack`/`kiVersion`, replay, wire, affinity, threading, allocation, shader, build, or live-verification exposure. Pre-stage architectural choices instead of deciding them. Do not add unit tests or unsupported implementation detail.

Derive the future implementation's Change Workflow Tier 1/2/3 from the highest root `AGENTS.md` risk trigger and record that trigger in the plan; queue `tier` is the separate informal `Quick Win`/`Small`/`Medium`/`Large`/`Architectural` descriptor. Choose `Effort`, `Impact`, and `Risks` from the canonical anchors, compute `Score = Effort - Impact + Risks`, and normally map the queue tier to the matching Effort anchor, elevating it only when concrete coordination or architectural risk makes the lower label misleading. Write one outcome/exposure sentence for row `notes`. Put directional prerequisites only in `dependsOn`. Put mandatory nondirectional constraints in reciprocal standard `## Coordination` sections in every affected plan.

### 3. Apply exactly one case

| Case | Tracked plan bytes | Queue action | Completion route |
|---|---|---|---|
| New independent plan | Write the final plan directly under `Documents/Plans/<area>/`. | Write a unique schema-version `1` `operation: "add"` request under `Temp/`; do **not** invoke WorktreeCli, request a receipt, or unlock anything. | Return the request path for `/verify-changes`, then `/finalize-changes` as `-PlanAddRequestPaths`; finalization publishes after landing. |
| Existing plan, prose only | Edit the live tracked plan directly, including reciprocal Coordination prose. | None when identity, `tier`, `effort`, `impact`, `risks`, `notes`, and `dependsOn` are unchanged. | Verify the tracked edit; wrapper completion routes through `/verify-changes` and `/finalize-changes`. |
| Existing plan, row changes | Leave the live plan untouched. Write its complete replacement under `Temp/`. | Write and immediately submit one schema-version `1` `operation: "update"` request. WorktreeCli atomically publishes the staged bytes and replacement row or neither. | Require the update receipt/unlocks and session validation, then route the queue mutation through `/verify-changes` and `/finalize-changes`. |

An add request contains prerequisite-first `sequences`; independent plans use separate sequences. Each entry supplies `queue`, normalized `plan`, queue `tier`, `effort`, `impact`, `risks`, `notes`, and optional `dependsOn`; omit `score` because WorktreeCli computes it.

An update entry supplies normalized `plan`, `expectedPlanSha256` from the untouched live bytes, `expectedRowSha256` from validation, repository-relative `stagedContent` beneath `Temp/`, every replacement row field, and optional `dependsOn`. Invoke:

```text
plan order update --repo <common-dir> --worktree <session-worktree> --owner <wrapper-owner> --session <same-wrapper-owner> --request <Temp repo-relative JSON>
```

Require a receipt naming every updated plan and successful unlocks, then rerun session validation and require exit `0` and `ok: true`. A claimed target or hash conflict must leave both plan and rows unchanged; report owner/conflict evidence and never retry through direct row edits. A failed add publication is owned by finalization; preserve its plan and request for the documented retry path.

## Report

Include every candidate exactly once:

```text
Created:
- <plan path> — <gap; add request path or proposed row>

Updated existing:
- <plan path or none> — <prose-only or atomic row update; receipt when submitted>

Duplicate mappings:
- <residual> -> <existing plan path, or none>

Scoring and coordination:
- <plan> — Change Workflow Tier; queue Tier; Effort/Impact/Risks/Score; dependencies/Coordination

Verification/finalization handoff:
- <validate evidence; staged add request paths; required route>

Files changed + regions touched:
- <path> — <heading/region>
- none

Residuals:
- <unqueued item, conflict, or blocker and reason, or none>
```

Never report a staged add as published, a prose-only edit as a queue mutation, or completion while a submitted update failed to unlock.
