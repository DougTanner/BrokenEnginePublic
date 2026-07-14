---
name: create-follow-up-plans
description: Converts unresolved C++ Code Change Process findings and residuals into concise, evidence-backed follow-up plans under `Documents/Plans/<area>/`, then adds correctly scored `Order.md` rows and overlap metadata. Use for step 11 whenever accepted structural work, out-of-scope fixes, or other non-trivial residuals cannot be completed safely in the current change. Also use when asked to queue review findings without duplicating existing plans.
allowed-tools: [Read, Write, Edit, Glob, Grep, PowerShell]
---

# Create Follow-up Plans

Turn unresolved process findings into executable debt plans without losing the evidence or acceptance gap that caused the handoff.

## Inputs

Require the caller to provide:

- source report paths plus stable IDs for unresolved findings and residuals, including originating process step;
- supporting evidence and affected symbols/files loaded from those indexed report sections;
- the current plan or intent summary and the acceptance criterion each item prevents;
- prior reviewer conclusions, user decisions, and related residuals;
- session changed-file list when overlap with the active change matters.
- for a delegated call, an absolute caller-assigned `ReportPath` under the session worktree's `Temp/AgentReports/`.
- a wrapper-created worktree with a live AgentCli session claim; this skill never creates a worktree or initializes an independent exclusion domain.

If a required fact is absent, inspect the repository and originating plan before proceeding. Do not invent evidence or intended behavior. Report items that still cannot be grounded as residuals instead of creating speculative plans.

## Workflow

### 1. Load planning rules

Read `Documents/AGENTS.md`, `Documents/Plans/AGENTS.md`, and `Documents/Plans/Order.md` completely. Follow their current plan shape, scoring anchors, sorting rules, dependency rules, and file-group rules; they are authoritative if this skill drifts.

Use `Documents/Plans/` only for refactors, bug fixes, hardening, and structural debt. If an item's purpose is a new engine capability, do not disguise it as debt: report that classification conflict for the main agent to resolve.

### 2. Validate every candidate

For each item:

1. State the concrete acceptance gap: what required condition remains false and which originating criterion, accepted finding, or residual establishes the requirement.
2. Confirm the root cause against current source or direct logs. Record durable evidence as `path:line`, symbol name, and observed behavior; prefer symbols over line numbers when one must carry the identity.
3. Confirm the work is still unresolved and is too structural, broad, or out of scope to fix in the active change.
4. Preserve any user decision and the authority relationship among user direction, the grilled plan, documentation, and current behavior. Surface contradictions rather than choosing silently.

Reject stale, disproven, already-fixed, purely stylistic, or evidence-free candidates. Report why each rejected item was not queued.

### 3. Reconcile duplicates before writing

Search all live plan files, `Order.md` rows, dependency entries, and file groups using the affected symbols, files, root-cause terms, and intended outcome.

- Treat an existing plan as a duplicate when it owns the same root cause and implementation boundary, even if its title differs. Map the residual to that plan and create nothing.
- If an unclaimed existing plan owns the root cause but omits a necessary acceptance gap, extend that plan with the verified evidence, design work, acceptance criterion, and exposure notes; update its `Order.md` score/Notes and sorted position if scope changed materially.
- Before editing an existing plan, use `$ROOT\Tools\AgentCli\Platforms\VisualStudio2026\Output\AgentCli.exe` to run `plan row status --repo <canonical-git-common-dir> --order Documents/Plans/Order.md --plan <normalized-Order.md-relative-path>`. If the executable is missing, stop; this skill never builds it or writes through a shared Output link. Treat a reported row claim as authoritative and do not edit it. Report the owner, matching path, and missing scope so the main agent can coordinate. Claim metadata exists only in AgentCli coordination state, never in `Order.md` or the plan file.
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

### 6. Coordinate, revalidate, and write

Finish plan drafting and scoring analysis outside the queue lock. Resolve `git rev-parse --git-common-dir` to a canonical absolute path, generate an owner with the verified checkout AgentCli's `lock token`, then acquire and release with this control flow:

```powershell
$QueueJson = (& $AgentCli plan queue lock --repo $GitCommonDir --order Documents/Plans/Order.md --owner $Owner --session $Label --worktree $ROOT | Out-String)
$LockExitCode = $LASTEXITCODE
if ($LockExitCode -ne 0) {
    throw "plan queue lock failed with exit code $LockExitCode"
}
$QueueOwned = $true
try {
    $QueueSnapshot = $QueueJson | ConvertFrom-Json
    if ($QueueSnapshot.queue.ownedByRequester -ne $true) {
        throw "plan queue lock did not return an owned snapshot"
    }
    # Revalidate, mutate, and verify while owned.
}
finally {
    if ($QueueOwned) {
        & $AgentCli plan queue unlock --repo $GitCommonDir --order Documents/Plans/Order.md --owner $Owner
        $UnlockExitCode = $LASTEXITCODE
        if ($UnlockExitCode -ne 0) {
            throw "plan queue unlock failed with exit code $UnlockExitCode"
        }
        $QueueOwned = $false
    }
}
```

Require lock exit `0` and response shape `{ "queue": <metadata>, "claims": [...] }` with `queue.ownedByRequester == true`, then treat the sorted `claims` array as authoritative. From successful acquisition until successful unlock, any failure or exception must run the owner unlock before propagating. While holding the lock, re-read `Documents/Plans/Order.md`, the live plan paths implicated by duplicate analysis, and relevant dependency/file-group entries. Re-run duplicate and claimed-row checks against that state. If an existing target is now claimed, release before reporting its owner. If repository drift requires new research or user input, release before doing that work, then reacquire and restart this revalidation. Never wait, delegate, report, or perform expensive analysis while holding the queue lock.

Score each plan from the canonical anchors in `Documents/AGENTS.md`:

- choose `Effort`, `Impact`, and `Risks` from repository evidence;
- compute `Score = Effort - Impact + Risks`;
- choose the informal `Tier` consistent with the plan's size and risk;
- write a one-line `Notes` cell describing the concrete outcome and important exposure.

Write each revalidated plan and add its complete row to `Documents/Plans/Order.md` at the current score-correct sorted position in the same locked mutation. Never leave an unindexed or partially scored plan. Add or update `## Dependencies` and `## File Groups` when the new or expanded plan overlaps live plans; do not turn ordinary overlap into a blocking dependency. Do not write coordination metadata into repository files.

Before release, verify that every new plan path resolves from its row, every score arithmetic result is correct, every referenced live plan exists, and no orphan plan or stale metadata was introduced. Capture the owner-unlock exit code as shown above and require `0`. Report only after successful release; if release fails, stop and report the queue owner and locator without continuing into other user interaction.

## Report

Follow [`../../references/subagent-reporting.md`](../../references/subagent-reporting.md).
For a delegated call, require `ReportPath`; after successful queue unlock,
write this complete report there and return only the compact indexed envelope.
Keep lock-release state, created/updated paths, unqueued items, and blockers
visible in the envelope. With no delegated `ReportPath`, retain the complete
inline report. Never report completion while holding the queue lock:

```text
Created:
- <plan path> — <acceptance gap queued>

Updated existing:
- <plan path or none> — <scope/row/dependency change>

Duplicate mappings:
- <residual> -> <existing plan path, or none>

Order.md:
- <exact row added or updated>
- <dependency/file-group updates, or none>

Files changed + regions touched:
- <path> — <heading/row/region>
- none

Residuals:
- <unqueued item and reason, or none>
```

Include every candidate in exactly one of Created, Updated existing, Duplicate mappings, or Residuals so no handoff disappears.
