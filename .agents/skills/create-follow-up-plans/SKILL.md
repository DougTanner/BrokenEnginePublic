---
name: create-follow-up-plans
description: Converts proven pre-existing or out-of-scope Change Workflow residuals into concise, evidence-backed follow-up Plans under `Documents/Plans/<area>/` with tracked scheduler metadata. Do not route an in-scope acceptance failure out of the active change. Also use when asked to record review findings without duplicating existing Plans.
allowed-tools: [Read, Write, Edit, Glob, Grep, PowerShell]
---

# Create Follow-up Plans

Turn eligible residuals into executable debt Plans. This skill owns candidate adjudication, grouping, duplicate detection, area and filename placement, collision handling, dependencies, metadata, and Coordination decisions; callers supply evidence, not those decisions.

## Inputs and boundary

Require direct finding evidence, originating stage and acceptance gap, affected symbols/files, prior reviewer or user decisions, the active intent/plan, related residuals, and the session changed-file list. Inspect missing facts; never invent evidence or behavior.

Read `Documents/AGENTS.md` and `Documents/Plans/AGENTS.md` completely. Their current Plan shape, metadata, dependency, and Coordination rules override this skill. This skill creates debt Plans only. Report a capability addition for main-agent routing to manual `Documents/Features/`; do not disguise it as debt.

Reject an in-scope acceptance failure, including required structural work: it remains a blocker in the active change. Also reject stale, disproven, fixed, stylistic-only, and evidence-free candidates, stating why.

Plan files and their byte-zero metadata are ordinary tracked Git content; write
them directly, with or without a live Plan claim. `Documents/Features` remains
manual and is not an alternate executable-Plan store.

## Workflow

### 1. Prove and consolidate candidates

For every candidate:

1. State the false required condition and its originating criterion, accepted finding, or residual.
2. Confirm root cause and unresolved state from current source or direct logs. Record durable `path:line`, symbol, and observed behavior evidence.
3. Prove it is pre-existing or outside the approved implementation boundary.
4. Preserve user decisions and the authority order among user direction, approved plan, documentation, and current behavior; report contradictions.

Group items only when root cause, implementation boundary, invariant, and verification strategy all match. Split independently landable work, architectural decisions, separate subsystems, or materially different risks.

Run the provisioned `Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe plan validate --repo <absolute-git-common-dir> --worktree <checkout> --baseline <commit>` before duplicate decisions and after tracked Plan edits; outside a wrapper session that baseline is `HEAD`. If the executable is absent, stop and report that authorized primary maintenance through `/compile` is required. Require exit `0`, the versioned JSON contract, `status: valid`, and `code: ok`; record stale dependency notices and healed claims. Treat `plans` entries as the executable inventory; never inspect machine-local claims.

Search all live plan files by symbols, paths, root-cause terms, outcome, and `## Coordination`. A plan is a duplicate when it owns the same root cause and implementation boundary. Map the candidate to it unless the proven acceptance gap requires extending that plan.

### 2. Draft and classify

Choose the existing owning area and a concise PascalCase filename; never overwrite a collision. Draft the smallest decision-complete plan with `# Title`, `## Context`, `## Design`, `## Critical files`, `## Out of scope`, `## Acceptance criteria` when the diff is insufficient, and `## Notes`. Include verified root cause, originating gap, implementation boundary, and applicable determinism/CRC, serialization/`.pack`/`kiVersion`, replay, wire, affinity, threading, allocation, shader, build, or live-verification exposure. Pre-stage architectural choices instead of deciding them. Do not add unit tests or unsupported implementation detail.

Derive the future implementation's Change Workflow Tier 1/2/3 from the highest root `AGENTS.md` risk trigger and record that trigger in the Plan. The first bytes are the immutable v1 metadata marker with canonical `createdUtc` and ordinal-sorted, unique `dependsOn` paths. Put only directional prerequisites in `dependsOn`; put mandatory nondirectional constraints in reciprocal standard `## Coordination` sections in every affected Plan. Do not add score, effort ranking, queue tier, row, request-file, or claim data.

### 3. Apply exactly one case

| Case | Tracked Plan bytes | Completion route |
|---|---|---|
| New Plan | Write the final Plan directly under `Documents/Plans/<area>/` with its immutable v1 marker. | Validate the tracked tree; route through `/verify-changes` and `/finalize-changes` only when a final-evidence gate applies. |
| Existing Plan, prose only | Edit the live tracked Plan directly, including reciprocal Coordination prose; preserve its marker byte-for-byte. | Validate and finalize the tracked edit. |
| Existing Plan, dependency change | Edit only the marker's `dependsOn` array plus required reciprocal Coordination prose; never change `createdUtc`. | Validate and finalize the tracked edit. |

Never create a request file, row, score, publication transaction, or claim.

## Report

Include every candidate exactly once:

```text
Created:
- <Plan path> — <gap and metadata>

Updated existing:
- <Plan path or none> — <prose-only or dependency update>

Duplicate mappings:
- <residual> -> <existing plan path, or none>

Tier and coordination:
- <Plan> — Change Workflow Tier/trigger; dependencies/Coordination

Verification/finalization handoff:
- <plan validate evidence; required route>

Files changed + regions touched:
- <path> — <heading/region>
- none

Residuals:
- <unrecorded item, conflict, or blocker and reason, or none>
```

Never call a written Plan claimed, published, or landed before the corresponding
scheduler or finalization evidence exists.
