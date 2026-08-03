---
name: create-follow-up-plans
description: Converts proven pre-existing or out-of-scope Change Workflow residuals into concise, evidence-backed follow-up Plans under `Documents/Plans/<area>/` with tracked scheduler metadata. Do not route an in-scope acceptance failure out of the active change. Also use when asked to record review findings without duplicating existing Plans, and for tooling-friction follow-ups recorded at a /next-plan claim exit.
allowed-tools: [Read, Write, Edit, Glob, Grep, PowerShell]
---

# Create Follow-up Plans

Turn eligible residuals into executable debt Plans. This skill owns the decision on each proposed follow-up, grouping, duplicate detection, area and filename placement, collision handling, dependencies, metadata, and Coordination decisions; callers supply evidence, not those decisions.

## Inputs and boundary

Require direct finding evidence, originating step and unmet acceptance criterion, affected symbols/files, prior reviewer or user decisions, the active intent/plan, related residuals, and the session changed-file list. Inspect missing facts; never invent evidence or behavior.

Read `Documents/AGENTS.md` and `Documents/Plans/AGENTS.md` completely. Their current Plan shape, metadata, dependency, and Coordination rules override this skill. This skill creates debt Plans only. Report a capability addition for main-agent routing to manual `Documents/Features/`; do not disguise it as debt.

Reject an in-scope acceptance failure, including required structural work: it remains a blocker in the active change. Also reject stale, disproven, fixed, stylistic-only, and evidence-free proposals, stating why.

Tooling friction observed during a `/next-plan` run is a separate category. For tooling-friction proposals only, the substitutions below replace the corresponding root-cause-based requirements above and in `## Workflow` — the proof steps, the grouping and duplicate rules, and the drafted Plan's verified root cause; every other requirement still applies, and ordinary proposals keep the full root-cause bar unchanged. The observed in-session symptom with its citation — exact command or script path, observed output or malformed result, and the rework or workaround it forced — substitutes for the unmet acceptance criterion or accepted residual, and for the confirmed root cause wherever one is required, including in the drafted Plan. Proven root cause is deferred to the `/next-plan-review` session named in the Plan's `## Design`. For the pre-existing and out-of-scope proof, verify that the misbehaving skill or script is outside the claimed Plan's `## In scope`; when it is inside, the failure is an in-scope blocker of the active change and is rejected as a friction follow-up. Grouping and duplicate detection key on (skill or script, observed symptom) instead of root cause; re-observing an already-recorded symptom updates nothing and is rejected as a duplicate. Evidence-free proposals — "the skill felt awkward", no citation — stay rejected.

Plan files and the metadata line that must be their very first bytes are
ordinary tracked Git content; write them directly, with or without a live Plan
claim. `Documents/Features` remains manual and is not an alternate
executable-Plan store.

## Workflow

### 1. Prove and consolidate proposals

For every proposal:

1. State the false required condition and its originating criterion, accepted finding, or residual.
2. Confirm root cause and unresolved state from current source or direct logs. Record durable `path:line`, symbol, and observed behavior evidence.
3. Prove it is pre-existing or outside the approved implementation boundary.
4. Preserve user decisions and the authority order among user direction, approved plan, documentation, and current behavior; report contradictions.

Group items only when root cause, implementation boundary, invariant, and verification strategy all match. Split independently landable work, architectural decisions, separate subsystems, or meaningfully different risks.

Run the provisioned `Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe plan validate --repo <absolute-git-common-dir> --worktree <checkout>` before duplicate decisions and after tracked Plan edits. If the executable is absent, stop and report that authorized primary maintenance through `/compile` is required. Require exit `0`, the versioned JSON contract, `status: valid`, and `code: ok`; record stale dependency notices and healed claims. Treat `plans` entries as the executable inventory; never inspect machine-local claims.

Search all live plan files by symbols, paths, root-cause terms, outcome, and `## Coordination`. A plan is a duplicate when it owns the same root cause and implementation boundary. Map the proposal to it unless the proven unmet acceptance criterion requires extending that plan.

### 2. Draft and classify

Choose the existing owning area and a concise PascalCase filename; never overwrite a collision. Draft the smallest decision-complete plan with `# Title`, `## Context`, `## Design`, `## Critical files`, `## In scope`, `## Out of scope`, `## Acceptance criteria` when the diff is insufficient, and `## Notes`. Include verified root cause, originating gap, implementation boundary, and applicable determinism/CRC, serialization/`.pack`/`kiVersion`, replay, wire, affinity, threading, allocation, shader, build, or live-verification exposure. Pre-stage architectural choices instead of deciding them. Do not add unit tests or unsupported implementation detail.

Derive the future implementation's Change Workflow Tier 1/2/3 from the highest root `AGENTS.md` risk trigger and record that trigger in the Plan. Put only directional prerequisites in `dependsOn`; put mandatory nondirectional constraints in reciprocal standard `## Coordination` sections in every affected Plan. Do not add score, effort ranking, queue tier, queue row, request file, or claim data.

Draft a tooling-friction body from this template. Its `Worktree` line is a profile-relative locator, never an absolute path, and no transcript path or transcript text ever enters the body:

```markdown
# Fix: <skill or script> — <one-line symptom>

## Context
<Observed symptom: exact command run, script path(:line if known), observed
output/behavior, what was repeated or worked around. No transcript text.>

Session provenance (machine-local; not reproducible after cleanup):
- Client: claude | codex
- Session: <lowercase uuid>
- Session branch: <claude|codex>/<uuid>
- Worktree: <profile-relative locator, e.g. .claude\worktrees\<repo>\<uuid> —
  never an absolute path, so no home prefix enters the public repo>
- Landing commit: `git log --diff-filter=A --format=%H -- <this plan path>`
- Run the review before /cleanup-worktrees removes this worktree: Codex
  transcript discovery requires the producing worktree to remain registered,
  and Claude review requires the exact session id above.

## Design
In a new session, run `/next-plan-review <landing commit>` supplying the
recorded client and session id, root-cause the friction from the proven
transcript, then make the smallest fix inside the `## In scope` boundary below.
If root-causing shows the fix lies outside that boundary, surface it for
re-planning instead of expanding scope.

## Critical files
- <each concrete SKILL.md and/or script file involved — these files are the
  authorized fix boundary>

## In scope
- Root-cause investigation via /next-plan-review with the recorded provenance
- The smallest resulting fix, confined to <the files named above, naming
  sections/functions when the observed symptom already identifies them>

## Out of scope
- The landed change the session produced
- Unrelated skills/scripts; any transcript path or transcript text in the repo

## Risk tier and invariants
Expected Tier 2 (scoped tool behavior); escalate if the fix reaches
build/bootstrap coordination. Never embed transcript paths or home paths.

## Acceptance criteria
- The recorded symptom no longer reproduces under the documented invocation
- /validate-skill passes for any changed SKILL.md; plan validate exits 0
```

Create the file with the repository-owned `.agents/scripts/New-PlanFile.ps1`:

```powershell
$RepositoryRoot = (git rev-parse --show-toplevel).Trim()
$Script = Join-Path $RepositoryRoot '.agents/scripts/New-PlanFile.ps1'
pwsh -NoProfile -File $Script -Area <existing area> -Name <PascalCase.md> -Body <body file path> -DependsOn <plan paths as one comma-separated token>
```

Its parameters, the `-DependsOn` single-token rule, its result shape, and its exit handling are in `../../references/new-plan-file.md`.

### 3. Apply exactly one case

| Case | Tracked Plan bytes | Completion route |
|---|---|---|
| New Plan | Run `New-PlanFile.ps1` (step 2) to write the final Plan under `Documents/Plans/<area>/`; it mints the marker and never overwrites an existing path. | Take the tree validation from the script's folded result; route through `/verify-changes` and `/finalize-changes` only when a landing gate applies. |
| Existing Plan, prose only | Edit the live tracked Plan directly, including reciprocal Coordination prose; preserve its marker byte-for-byte. | Validate and finalize the tracked edit. |
| Existing Plan, dependency change | Edit only the marker's `dependsOn` array plus required reciprocal Coordination prose; never change `createdUtc`. | Validate and finalize the tracked edit. |

Never create a request file, row, score, claim, or a commit that publishes a row.

## Report

Include every proposal exactly once:

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

Report a written Plan as tracked content; do not describe it as claimed or
landed until that has actually happened.
