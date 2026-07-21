---
name: save-plan
description: Save an explicitly supplied, complete plan-mode proposal into the correct Broken Engine planning tree and prepare its approval-bound WorktreeCli add request. Use only when the user explicitly invokes `/save-plan` or `$save-plan`, optionally with a PascalCase Markdown filename override.
argument-hint: [PascalCase.md]
allowed-tools: [Read, Write, Edit, Glob, Grep, PowerShell]
disable-model-invocation: true
---

# Save Plan

Persist one complete plan-mode proposal and stage its queue request for landing.

## Preconditions and source

Require a registered wrapper session: confirm the current checkout is a registered Git worktree and equals both `BROKEN_ENGINE_WORKTREE_PATH` and `BROKEN_ENGINE_WORKTREECLI_SESSION_WORKTREE`, `BROKEN_ENGINE_WORKTREECLI_ADMISSION_MODE` is `session`, and `BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER` is nonempty. Stop otherwise.

Use exactly one client-provided source:

- Codex: the latest complete `<proposed_plan>...</proposed_plan>` block in the current conversation. Remove the enclosing tags and preserve the body.
- Claude Code: an absolute plan-file path explicitly supplied with the invocation. Read exactly that file.

Never list, search, or infer a plan from either client's home plan store. Stop and request the required source when it is absent, incomplete, or ambiguous.

## Validate and place

1. Read `Documents/AGENTS.md` and the destination tree's `AGENTS.md`. Require the body to satisfy the current plan-file contract, including its decision-complete design, out-of-scope boundary, risk trigger and exposed invariants, and observable acceptance criteria when needed. Never invent a missing decision; ask the user.
2. Run the provisioned WorktreeCli `plan order validate --repo <absolute-common-dir> --worktree <checkout>`. Require exit `0` and `ok: true`; record non-blocking stale-baseline `missing-plan-file` notices and treat returned `rows` as the only executable inventory. Search live plan files to reject duplicates and identify dependencies or reciprocal `## Coordination` edits. Never read or edit the queue store.
3. Classify the body into `Documents/Plans/` or `Documents/Features/`, choose an existing subject-area directory, and derive a concise PascalCase filename from the title. Ask when classification or area is materially ambiguous.
4. If supplied, require the filename override to case-sensitively match `^[A-Z][A-Za-z0-9]*\.md$`; reject separators, directories, other extensions, and non-PascalCase names. Never overwrite a collision; request another name.
5. Ground queue tier, effort, impact, risks, notes, and normalized directional `dependsOn` identities in the body, inventory, and canonical anchors. Stop for any material unsupported choice.

## Save and stage

Write the tag-free plan body to `<tree>/<area>/<filename>`. Write a uniquely named request under the session worktree's `Temp/` using schema version `1`, operation `add`, and one independent sequence:

```json
{"schemaVersion":1,"operation":"add","sequences":[[{"queue":"plans","plan":"Documents/Plans/<area>/<filename>","tier":"Small","effort":2,"impact":3,"risks":1,"notes":"One-line outcome","dependsOn":["Documents/Plans/<area>/<prerequisite>.md"]}]]}
```

Use matching `features` identities for features. Omit `dependsOn` when empty and never supply `score`. Do not invoke `plan order add`, request a receipt, mutate or unlock queue state, or publish the row.

Report the plan path, request path, queue, scoring inputs, dependencies and Coordination, source/contract/inventory checks, and changed files. Pass the complete staged-request path list through `/verify-changes` to `/finalize-changes`; finalization owns post-landing publication.
