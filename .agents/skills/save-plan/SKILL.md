---
name: save-plan
description: Save an explicitly supplied complete plan-mode proposal into the correct Broken Engine planning tree. Documents/Plans receives Git-backed metadata; Documents/Features remains manual.
argument-hint: [PascalCase.md]
allowed-tools: [Read, Write, Edit, Glob, Grep, PowerShell]
disable-model-invocation: true
---

# Save Plan

Persist exactly one complete client-supplied proposal. For Codex use the latest complete `<proposed_plan>` body; for Claude use the explicitly supplied absolute plan-file path. Do not discover or infer a source from a client-local plan store.

Read `Documents/AGENTS.md` and the selected tree's guidance. The saved plan is a file one model writes for another model to pick up: a different implementing model with no access to the planning conversation executes it verbatim, so the body must be the final actionable plan — self-contained, with no unresolved options, TBDs, or decisions deferred to the implementer beyond trivial naming and local detail. Require a decision-complete body: context, smallest design, critical files, out-of-scope boundary, applicable risk trigger/invariants, and observable acceptance criteria where the change needs them.

The body must carry an explicit scope contract: in-scope entries under a required `## In scope` heading, the boundary under a required `## Out of scope` heading, and the listed scope is both target and ceiling: the implementer makes the smallest complete change and adds no abstractions, configuration, refactors, or fixes to adjacent code it encounters. Scope is limited inside files, not only across them — in-scope entries name the specific functions, members, or regions to change, and naming a file grants no permission to touch anything in it beyond the named regions plus the mechanical necessities (includes, declarations) the named change requires. Every file a `## Coordination` clause obliges the implementer to edit must itself appear in the in-scope list with its named regions; a coordination obligation is not a substitute for scope.

Ask the user for a meaningfully missing decision — including ambiguity, a missing scope ceiling, or file-granularity-only scope — rather than invent or patch it.

Classify into `Documents/Plans/` for executable engine debt or `Documents/Features/` for manual capability planning. Select an existing area and a concise PascalCase filename matching `^[A-Z][A-Za-z0-9]*\.md$`. Search live Plans for duplicate root cause and implementation boundary before writing.

## Executable Plans

Write the plan body to a file, then create the Plan with the repository-owned `.agents/scripts/New-PlanFile.ps1`.

```powershell
$RepositoryRoot = (git rev-parse --show-toplevel).Trim()
$Script = Join-Path $RepositoryRoot '.agents/scripts/New-PlanFile.ps1'
pwsh -NoProfile -File $Script -Area <existing area> -Name <PascalCase.md> -Body <body file path> -DependsOn <plan paths as one comma-separated token>
```

Its parameters, the `-DependsOn` single-token rule, its result shape, and its exit handling are in `../../references/new-plan-file.md`; only the created outcome there permits reporting the plan as saved.

Record the folded result's invalid-plan diagnostics and stale-edge notices: existing dependency paths must remain executable to block a child, and missing paths are intentionally stale satisfied edges. Do not read local claims or use validation to choose, claim, or reorder work.

## Manual Features

Features are ordinary Markdown. Do not prepend scheduler metadata unless the user explicitly makes it an executable Plan, and never create claim records, request files, score fields, queue rows, or publication inputs for Features.

## Landing pre-approval

Invoking this skill is the user's explicit approval to commit or land the saved file (Plan or Feature) to the primary branch: its content was already approved during planning, and only that file lands. Per the authoritative landing-confirmation contract (`../finalize-changes/SKILL.md`, "Landing confirmation"), when the diff that changes primary consists solely of the saved file, this invocation is a yes given in advance that stays in effect, so `/finalize-changes` proceeds without asking the confirmation question; any additional changed file in the diff voids the exception and the entire change uses the landing gate.

## Report

Report source, selected path, duplicate outcome, tier trigger, dependency decision, validation command/result where applicable, and changed files. A saved Plan is tracked Git content, not a queue change and not a claim.
