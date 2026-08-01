---
name: external-skill-creator
description: Create, revise, or review repository skills. Use when the user explicitly requests external-skill-creator to define a skill workflow, improve SKILL.md instructions, design trigger descriptions or output formats, organize progressive disclosure, or audit skill quality and client compatibility.
allowed-tools: [Read, Write, Edit, Glob, Grep, Agent, Bash, PowerShell]
disable-model-invocation: true
---

# Skill Creator

Create or improve a skill from the user's intent, repository conventions, and evidence from existing workflows. Keep shared instructions client-neutral; isolate client syntax and invocation controls in `references/client-compatibility.md`.

## Workflow

1. Extract what the conversation already establishes: capability, trigger contexts, inputs, outputs, success criteria, dependencies, and corrections. Ask only for meaningful gaps.
2. Inspect applicable repository instructions and nearby skills. Read `../validate-skill/references/frontmatter-schema.md` before writing frontmatter and the compatibility reference before adding client-specific behavior.
3. Choose the smallest useful package: `SKILL.md` for the durable workflow; `references/` for details loaded on demand; `scripts/` for repeatable mechanics; `assets/` for output resources.
4. Draft imperative, general instructions. Explain constraints where the reason helps judgment. Preserve an existing skill's directory and frontmatter name unless the user requests a migration.
5. Re-read the result with fresh eyes. Remove duplicated guidance, speculative options, and examples that do not clarify a non-trivial requirement. Restate each rule positively as the action to take, keeping a prohibition only where it cannot be stated positively and pairing it with what to do instead, and end each workflow step on a checkable done-condition.
6. Run the repository `validate-skill` workflow on the finished skill. Fix every mechanical or Critical finding and rerun until it passes; treat `BLOCKED` as a stop condition.

Research inline when the answer is local and small. Use available documentation or delegated research only when the skill depends on behavior that needs external or multi-source evidence.

## Repository Conventions

- Store each skill at `.agents/skills/<name>/SKILL.md`.
- Treat `external-` as a naming convention, not an invocation policy. Configure implicit invocation and chaining per client from the actual workflow.
- Keep substantive trigger contexts in `description` or `when_to_use`; metadata is the text agents search when deciding whether to use the skill, and may be truncated.
- Use the shared frontmatter schema as the sole repository contract. Do not copy fields from a client installation into repository frontmatter without extending the schema and validator together.
- Keep the body lean because it remains in context after invocation. Measure Markdown with `pwsh -NoProfile -File .agents/scripts/Measure-Tokens.ps1 -Path <file>`; add a table of contents to any reference over 2,000 `bt-token-v1`.
- Write `scripts/` in PowerShell 7. Use Python only when a Python-only runtime or library forces it (RenderDoc, Gaea 2 terrain tooling, the code-quality-metrics analyzer). Host Python is located through `.agents/scripts/Detect-Python.ps1` (x64 CPython 3.12+, the repository-wide floor) or the skill's own pinned bootstrap; an external tool's embedded or version-matched interpreter (RenderDoc, per the agent-harness renderdoc reference) follows that tool's rules instead. A new Python script requires explicit justification.

## Writing Guidance

### Description

Lead with the capability, then name concrete user intents and specialized contexts. Do not encode invocation policy only in prose; apply the matching client controls.

### Progressive Disclosure

Keep selection logic and the end-to-end workflow in `SKILL.md`. Move variant-specific tables, API details, long examples, and client syntax to focused references. Bundle a script when future invocations would otherwise recreate the same deterministic helper.

### Outputs

Define an exact template only when downstream work consumes it or consistent structure is part of success. Show one short input/output example for a non-trivial format.

## Completion

Report changed package files, decisive validation evidence, token measurements, and unresolved compatibility or workflow decisions. Do not claim cross-client support unless each intended client's controls were configured and checked.
