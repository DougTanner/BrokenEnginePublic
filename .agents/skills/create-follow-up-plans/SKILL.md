---
name: create-follow-up-plans
description: Converts proven pre-existing or out-of-scope Change Workflow residuals into concise, evidence-backed follow-up plans under `Documents/Plans/<area>/`, then submits structured WorktreeCli add/update requests. Do not route an in-scope acceptance failure out of the active change. Also use when asked to queue review findings without duplicating existing plans.
allowed-tools: [Read, Write, Edit, Glob, Grep, PowerShell]
---

# Create Follow-up Plans

Turn eligible proven pre-existing or out-of-scope residuals into executable debt plans without losing the evidence or acceptance gap that caused the handoff.

## Inputs

Require the caller to provide:

- direct finding/residual evidence, including originating process stage and affected symbols/files;
- the current plan or intent summary and the acceptance criterion each item prevents;
- prior reviewer conclusions, user decisions, and related residuals;
- session changed-file list when overlap with the active change matters.
- a wrapper-created worktree with a live WorktreeCli session claim; this skill never creates a worktree or initializes an independent exclusion domain.

If a required fact is absent, inspect the repository and originating plan before proceeding. Do not invent evidence or intended behavior. Report items that still cannot be grounded as residuals instead of creating speculative plans.

## Workflow

### 1. Load planning rules

Read `Documents/AGENTS.md` and `Documents/Plans/AGENTS.md` completely. Follow their current plan shape, scoring anchors, structured dependency rules, and Coordination policy; they are authoritative if this skill drifts. Run `plan order validate --repo <canonical-git-common-dir> --worktree <session-worktree>` and require its JSON result to report `ok: true` before preparing a mutation. WorktreeCli is the only executable-row parser; the queue is machine-local state, so never parse or edit a queue row directly.

Use `Documents/Plans/` only for refactors, bug fixes, hardening, and structural debt. If an item's purpose is a new engine capability, do not disguise it as debt: report that classification conflict for the main agent to resolve.

### 2. Validate every candidate

For each item:

1. State the concrete acceptance gap: what required condition remains false and which originating criterion, accepted finding, or residual establishes the requirement.
2. Confirm the root cause against current source or direct logs. Record durable evidence as `path:line`, symbol name, and observed behavior; prefer symbols over line numbers when one must carry the identity.
3. Confirm the work is still unresolved and proven pre-existing or out of scope. An in-scope acceptance failure, including structural work, remains a blocker for the active change and is rejected from follow-up routing.
4. Preserve any user decision and the authority relationship among user direction, the grilled plan, documentation, and current behavior. Surface contradictions rather than choosing silently.

Reject stale, disproven, already-fixed, purely stylistic, or evidence-free candidates. Report why each rejected item was not queued.

### 3. Reconcile duplicates before writing

Search all live plan files using the affected symbols, files, root-cause terms, intended outcome, and standard `## Coordination` sections. Use the successful WorktreeCli validation result as the executable-row inventory and source of each existing row's `rowSha256`; do not derive row state from Markdown.

- Treat an existing plan as a duplicate when it owns the same root cause and implementation boundary, even if its title differs. Map the residual to that plan and create nothing.
- If an existing plan owns the root cause but omits a necessary acceptance gap, edit the plan body directly, then stage a WorktreeCli `update` request carrying the full edited plan bytes as `stagedContent` (with any replacement row fields). The direct prose edit belongs in `stagedContent`; the request is how those bytes and row data publish atomically.
- WorktreeCli checks row claims and expected hashes under both queue locks. A claimed target or hash conflict produces zero repository mutation; report the returned owner/conflict evidence and do not retry by editing rows directly. Claim metadata exists only in WorktreeCli coordination state, never in a plan file.
- Create a new plan when the work has an independent root cause or can be executed and accepted independently.

### 4. Group related residuals

Group items only when they share a root cause, implementation boundary, affected invariant, and verification strategy. Split them when they span independent subsystems, require separate architectural decisions, have materially different risk, or one could land while another remains blocked.

Prefer one cohesive plan over a miscellaneous review batch. A multi-item plan must explain why its items should land together.

### 5. Draft actionable plans

Choose the existing area directory matching the owning subsystem. Add a new area only when no current area fits. Use a concise PascalCase filename that describes the outcome; never overwrite a collision.

Prepare the smallest decision-complete plan content that preserves:

- `# Title`
- `## Context` — current behavior, verified root cause, evidence, and originating acceptance gap
- `## Design` — intended behavior and implementation boundary; pre-stage unresolved architectural choices instead of deciding them
- `## Critical files` — affected interfaces/symbols with paths
- `## Out of scope` — adjacent work explicitly excluded
- `## Acceptance criteria` — observable completion and verification conditions when completion is not self-evident
- `## Notes` — determinism/CRC, `kiVersion`/`.pack`, replay, wire protocol, client/server guard, allocation-tracked, shader, build, and live-verification exposure as applicable

Do not prescribe unsupported implementation details. Do not add unit tests. Require the builds, selective checks, or live agent-harness scenarios proportionate to the exposed behavior.

### 6. Build and submit the structured mutation

Finish evidence adjudication, plan drafting, scoring, grouping, semantic duplicate checks, and Coordination decisions before invoking WorktreeCli. Set `$WorktreeCli` to the current checkout's provisioned `Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe`; if it is missing, stop and report that explicitly authorized primary maintenance through `/compile` is required. Resolve `git rev-parse --git-common-dir` to a canonical absolute path and generate one owner with `lock token`.

Score each plan from the canonical anchors in `Documents/AGENTS.md`:

- choose `Effort`, `Impact`, and `Risks` from repository evidence;
- compute `Score = Effort - Impact + Risks`;
- choose the informal `Tier` consistent with the plan's size and risk;
- write a one-line `Notes` cell describing the concrete outcome and important exposure.

For new plans, write the final plan files first, then create a unique schema-version `1` JSON request beneath the session worktree's `Temp/` with `operation: "add"` and prerequisite-first `sequences`. Each entry supplies `queue`, normalized repository-relative `plan`, `tier`, `effort`, `impact`, `risks`, `notes`, and optional `dependsOn`; WorktreeCli computes Score and adds the immediate-predecessor edge within each sequence. Put independent plans in separate sequences and name already-live prerequisites explicitly. The add request is **staged, not submitted here** — `/finalize-changes` submits it post-landing (with `--worktree <session>`, whose tip equals the landed commit), so rows always reference landed plan files; a session that never lands leaves no rows and no orphans. The submitted command is:

```text
plan order add --repo <common-dir> --worktree <session-worktree> --owner <token> --session <label> --request <Temp repo-relative JSON>
```

For existing-plan extensions, write each proposed replacement beneath `Temp/`, hash the untouched live plan bytes, take `expectedRowSha256` from the validation result, and create one schema-version `1` request with `operation: "update"` and an `updates` array. Each update supplies `plan`, `expectedPlanSha256`, `expectedRowSha256`, `stagedContent`, replacement `tier`/`effort`/`impact`/`risks`/`notes`, and optional `dependsOn`. Invoke:

```text
plan order update --repo <common-dir> --worktree <session-worktree> --owner <token> --session <label> --request <Temp repo-relative JSON>
```

WorktreeCli publishes all entries or none. The `update` verb stages the full plan bytes it intends to publish: `expectedPlanSha256` hashes the bytes the session read before its own edits, so a same-session direct prose edit belongs in `stagedContent`, and a hash conflict signals another session's concurrent mutation (the intended guard). Fresh `add` plans, by contrast, are direct file writes staged for the post-landing submission.

Directional prerequisites exist only in `dependsOn`. Mandatory nondirectional constraints (`never interleave`, joint resolution, alone execution, or protocol/version/CRC/replay/`.pack`/`kiVersion` batching) require reciprocal standard `## Coordination` sections in every affected live plan. Those sections are plan-body prose edited directly; the authoring rule is to update every existing counterpart in the same change set, and a concurrent counterpart edit resolves as a merge conflict at landing, not a zero-mutation queue failure. Ordinary warning-only overlap may remain one-sided in plan prose.

When a row update is submitted, require its receipt to identify every intended plan and successful queue unlocks, then rerun session `plan order validate` and require `ok: true`. The staged add publishes at landing, so a session that never lands leaves retryable new plan files and its request as session orphans; a failed update leaves live rows unchanged. Do not repair either failure by editing a queue row by hand.

## Report

After successful queue unlock, return this complete report inline. Keep
lock-release state, created/updated paths, unqueued items, and blockers visible.
Never report completion while holding the queue lock:

```text
Created:
- <plan path> — <acceptance gap queued>

Updated existing:
- <plan path or none> — <scope/row/dependency change>

Duplicate mappings:
- <residual> -> <existing plan path, or none>

WorktreeCli receipt:
- <add/update request path and exact receipt identity>
- <structured dependencies and Coordination updates, or none>

Files changed + regions touched:
- <path> — <heading/row/region>
- none

Residuals:
- <unqueued item and reason, or none>
```

Include every candidate in exactly one of Created, Updated existing, Duplicate mappings, or Residuals so no handoff disappears.
