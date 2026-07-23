---
name: save-plan
description: Save an explicitly supplied complete plan-mode proposal into the correct Broken Engine planning tree. Documents/Plans receives Git-backed metadata; Documents/Features remains manual.
argument-hint: [PascalCase.md]
allowed-tools: [Read, Write, Edit, Glob, Grep, PowerShell]
disable-model-invocation: true
---

# Save Plan

Persist exactly one complete client-supplied proposal. For Codex use the latest complete `<proposed_plan>` body; for Claude use the explicitly supplied absolute plan-file path. Do not discover or infer a source from a client-local plan store.

Read `Documents/AGENTS.md` and the selected tree's guidance. Require a decision-complete body: context, smallest design, critical files, out-of-scope boundary, applicable risk trigger/invariants, and observable acceptance criteria where the change needs them. Ask the user for a materially missing decision rather than invent it.

Classify into `Documents/Plans/` for executable engine debt or `Documents/Features/` for manual capability planning. Select an existing area and a concise PascalCase filename; an override must match `^[A-Z][A-Za-z0-9]*\.md$`, have no directory component, and not overwrite a live path. Search live Plans for duplicate root cause and implementation boundary before writing.

## Executable Plans

A new Plan begins at byte zero, UTF-8 without a BOM:

```text
<!-- broken-engine-plan/v1 {"createdUtc":"<immutable UTC>","dependsOn":[]} -->
```

Both keys are mandatory. `createdUtc` never changes. Each dependency is a canonical case-sensitive `Documents/Plans/**/*.md` path; dependencies are unique and ordinal sorted. Existing dependency paths must remain executable to block a child; missing paths are intentionally stale satisfied edges.

Use the provisioned WorktreeCli to run `plan validate --repo <common> --worktree <checkout> --baseline <commit>` after saving. Record invalid-plan diagnostics and stale-edge notices. Do not read local claims or use validation to choose, claim, or reorder work.

## Manual Features

Features are ordinary Markdown. Do not prepend scheduler metadata unless the user explicitly makes it an executable Plan, and never create claim records, request files, score fields, queue rows, or publication inputs for Features.

## Report

Report source, selected path, duplicate outcome, tier trigger, dependency disposition, validation command/result where applicable, and changed files. A saved Plan is tracked Git content, not a queue mutation and not a claim.
