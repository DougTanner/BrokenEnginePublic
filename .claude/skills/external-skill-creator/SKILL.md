---
name: external-skill-creator
description: Create new skills and improve existing skills following best practices. Use when users want to create a skill from scratch, edit or revise an existing skill, review a skill for quality, or need guidance on skill structure, frontmatter, descriptions, writing patterns, or progressive disclosure. User-invoked via /external-skill-creator (the disable-model-invocation flag means Claude cannot invoke or chain to this skill).
allowed-tools: [Read, Write, Edit, Glob, Grep, Agent]
disable-model-invocation: true
---

# Skill Creator

Help the user create or improve a skill: understand what it should do, then draft or revise SKILL.md following the practices below. Be flexible — some users iterate collaboratively, others want a quick draft.

## Repo Conventions (Broken Engine)

Sibling skills in this repo follow these conventions — match them when creating a new skill:

- **`external-` prefix** marks explicit-invocation skills (e.g., `external-grill-plan`, `external-design-interface`, `external-deep-analysis`). Most set `disable-model-invocation: true`, but the flag removes the skill from Claude's reach entirely (no auto-trigger, no Skill tool) — set it only when nothing, neither a documented workflow step nor Claude itself, needs programmatic invocation. Three intentional exceptions: `external-grill-plan` and `external-self-audit` (the main agent invokes them as steps 1 and 2b of the CLAUDE.md C++ Code Change Process) and `external-design-interface` (proactively suggests itself, which requires staying in the listing). User-only skills without the prefix (e.g., `gaea2-load`, `next-plan`) follow the same rule. Unprefixed proactive skills (e.g., `add-collection`, `repo-code-review`, `compile`) auto-trigger on matching contexts.
- **Directory layout**: each skill lives at `.claude/skills/<name>/SKILL.md` with optional `references/`, `scripts/`, `assets/` sidecars.
- **Frontmatter style**: `allowed-tools:` uses YAML array syntax `[Read, Edit, Write]` in this repo (the skill schema also accepts space- or comma-separated strings, but the repo is consistent on arrays).

## Creating a Skill

### Capture Intent

The current conversation might already contain the workflow to capture (e.g., the user says "turn this into a skill"). If so, extract answers from the history first — tools used, step sequence, corrections the user made, input/output formats — then have the user fill gaps and confirm. Either way, establish:

1. What should this skill enable Claude to do?
2. When should this skill trigger? (what user phrases/contexts)
3. What's the expected output format?

### Interview and Research

Proactively ask questions about edge cases, input/output formats, example files, success criteria, and dependencies.

Check available MCPs - if useful for research (searching docs, finding similar skills, looking up best practices), dispatch Opus subagents via the Agent tool in parallel when research spans multiple sources, otherwise inline. Come prepared with context to reduce burden on the user.

### Write the SKILL.md

Based on the user interview, fill in these components:

- **name** *(optional)*: Skill identifier. Defaults to the directory name if omitted. Lowercase letters, digits, hyphens; max 64 characters
- **description**: What the skill does and when to use it (see Description Best Practices below)
- **Optional behavior fields**: `when_to_use`, `disable-model-invocation`, `user-invocable`, `allowed-tools`, `disallowed-tools`, `paths`, `argument-hint`, `arguments`, `context` + `agent`, `model`, `effort`, `hooks`, `shell` (see Frontmatter Reference below)
- **the body**: The actual skill instructions

---

## Skill Structure

### Anatomy of a Skill

```
skill-name/
├── SKILL.md (required)
│   ├── YAML frontmatter (all fields optional; `description` recommended)
│   └── Markdown instructions
└── Supporting files (optional — any layout; reference them from SKILL.md)
    ├── scripts/    - Executable code Claude runs via Bash (not loaded into context)
    ├── references/ - Docs loaded only when Claude reads them
    ├── examples/   - Sample outputs demonstrating expected format
    └── assets/     - Files used in output (templates, icons, fonts)
```

Claude Code skills follow the [Agent Skills](https://agentskills.io) open standard; the `scripts/` + `references/` + `assets/` layout is a convention from Anthropic's own skill-creator, not a requirement. Any file layout works as long as SKILL.md points at the files it needs.

### Frontmatter Reference

All fields are optional. Only `description` is recommended so Claude can decide when to apply the skill.

**Core fields**
- `name`: Display name. Lowercase letters, digits, hyphens; max 64 chars. If omitted, the directory name is used.
- `description`: What the skill does and when to use it. Max 1,024 chars (API validation); combined with `when_to_use`, truncated at **1,536 characters** in the skill listing. Front-load the key use case.
- `when_to_use`: Additional trigger phrases or example requests. Appended to `description` in the listing; shares the 1,536-char cap.

**Invocation control**
- `disable-model-invocation: true` — Claude cannot invoke the skill at all (no auto-trigger, no Skill tool, no chaining from another skill); only the user can trigger it with `/name`. Also prevents preloading into subagents. Use for workflows with side effects (commits, deploys) or that must be user-initiated. When set, the description is **not loaded into context** — zero budget cost, so a long, human-readable description for `/help` and source-tree readers is free.
- `user-invocable: false` — hide from the `/` menu. Use for background-knowledge skills Claude should load automatically but users shouldn't type by hand. **Note**: this controls menu visibility only, not Skill-tool access — Claude can still invoke the skill programmatically. To block programmatic invocation, use `disable-model-invocation: true`.
- `paths`: Comma-separated string or YAML list of glob patterns. When set, Claude auto-loads the skill only when working with matching files.
- `argument-hint`: Autocomplete hint, e.g. `[issue-number]`.

**Execution environment**
- `allowed-tools`: Space- or comma-separated string, or YAML list. Grants per-use approval for the listed tools while the skill is active (does not restrict other tools). Supports fine-grained rules like `Bash(git add *)`. `disallowed-tools` is the inverse.
- `context: fork` — run the skill in a forked subagent. Pair with `agent: Explore | Plan | general-purpose | <custom>`.
- `model`: Override the session model for this skill.
- `effort`: `low | medium | high | xhigh | max` (available levels depend on model).
- `hooks`: Skill-scoped lifecycle hooks.
- `shell`: `bash` (default) or `powershell` — controls the shell for `` !`command` `` injection.

**String substitutions** (used inside the markdown body)
- `$ARGUMENTS` — the full argument string as typed. If the body omits `$ARGUMENTS` but the skill is invoked with arguments, Claude Code appends `ARGUMENTS: <value>` to the end of the skill content automatically, so user input is never silently dropped.
- `$ARGUMENTS[N]` or `$N` — positional argument by 0-based index (shell-quoted, so wrap multi-word values in quotes)
- `$name` — named positional arguments, when declared via the `arguments:` frontmatter field
- `${CLAUDE_SESSION_ID}` — current session ID
- `${CLAUDE_SKILL_DIR}` — directory containing the skill's SKILL.md (for referencing bundled scripts)
- `${CLAUDE_EFFORT}` — current effort level

**Inline shell execution** — a backtick-bang-command-backtick span (backtick, `!`, command, backtick) runs before Claude sees the prompt; the output replaces the placeholder. Use for injecting live data (PR diffs, git status, environment info). A multi-line form also exists: a fenced block whose info-string is just `!` (three backticks immediately followed by `!`). The policy setting `"disableSkillShellExecution": true` (typically managed settings) replaces each command with a disabled-by-policy notice instead of running it.

> Note: do **not** include a literal backtick-backtick-backtick-bang sequence inside a SKILL.md (even quoted inside a code span) — the harness's shell-injection parser will match it before markdown parsing runs, try to execute the rest of the file as a shell block, and abort the skill load. Describe the syntax in prose instead of pasting it.

**Extended thinking** — including the word `ultrathink` anywhere in the body enables extended-thinking mode while the skill runs.

### Progressive Disclosure

Skills use a three-level loading system:
1. **Metadata** (name + description) - Always in context (~100 words)
2. **SKILL.md body** - Loads on invocation and **stays in context for the rest of the session** — every line is a recurring token cost (<500 lines ideal)
3. **Bundled resources** - Loaded as needed (unlimited size; scripts can execute without being loaded into context)

**Key patterns:**
- If approaching the 500-line limit, move detail into reference files with clear pointers about when to read them
- For reference files longer than 100 lines, include a table of contents

**Domain organization**: When a skill supports multiple domains/frameworks, organize by variant:
```
cloud-deploy/
├── SKILL.md (workflow + selection)
└── references/
    ├── aws.md
    ├── gcp.md
    └── azure.md
```
Claude reads only the relevant reference file.

---

## Description Best Practices

Claude decides whether to consult a skill based solely on the name and description in its skill listing — the description is the entire discovery surface.

**Key principles:**
- Include both what the skill does AND specific contexts for when to use it
- Use imperative phrasing: "Use this skill for..." rather than "This skill does..."
- Focus on user intent (what they're trying to achieve) rather than implementation details
- Make descriptions distinctive and immediately recognizable — they compete with other skills for Claude's attention
- Claude tends to "undertrigger" skills, so make descriptions a little "pushy" — explicitly list contexts where the skill should be used, even if they seem obvious

**Example:**
- Weak: "How to build a simple fast dashboard to display internal data."
- Strong: "How to build a simple fast dashboard to display internal data. Use this skill whenever the user mentions dashboards, data visualization, internal metrics, or wants to display any kind of company data, even if they don't explicitly ask for a 'dashboard.'"

**Constraints:**
- Combined `description` + `when_to_use` is truncated at **1,536 characters** in the skill listing. Descriptions compete for a per-session character budget that scales at 1% of the context window (8,000-char fallback); when crowded, descriptions get shortened further. Front-load the key use case.
- Aim for 100-200 words — concise but comprehensive.
- If trigger phrases are long or numerous, move them into `when_to_use` so the primary `description` stays scannable.

**Prefer the frontmatter flag over prompt-level guards.** For manual-only skills, set `disable-model-invocation: true` — a hard guarantee, not a prompt hedge. Description language like "Never trigger autonomously" is **not a substitute**: the description still loads, still consumes the per-session budget, and Claude can still match against it. A description that self-declares manual-only without the flag is a Critical finding when auditing (a frequent misconfiguration in this repo's history). Conversely, if anything needs Claude to invoke the skill — a documented process step, proactive self-suggestion, chaining — the flag cannot be set; wanting both auto-trigger and manual-only is contradictory, so pick one and make the description match.

**Triggering behavior:** Claude only consults skills for tasks it can't easily handle on its own. Simple, one-step queries may not trigger a skill even if the description matches perfectly. Complex, multi-step, or specialized queries reliably trigger skills when the description matches.

---

## Writing Best Practices

### Style

- **Use imperative form** in instructions ("Read the file", not "You should read the file")
- **Explain the why** behind instructions rather than heavy-handed MUSTs. LLMs have good theory of mind — when given reasoning, they can go beyond rote instructions. If you find yourself writing ALWAYS or NEVER in all caps, reframe and explain the reasoning instead
- **Keep it general** — use theory of mind and avoid making skills super-narrow to specific examples
- **Keep the prompt lean** — remove things that aren't pulling their weight. If instructions cause the model to waste time on unproductive work, cut them
- **Draft, then revise** — write a draft, look at it with fresh eyes, and improve it

### Defining Output Formats

```markdown
## Report structure
ALWAYS use this exact template:
# [Title]
## Executive summary
## Key findings
## Recommendations
```

### Including Examples

```markdown
## Commit message format
**Example 1:**
Input: Added user authentication with JWT tokens
Output: feat(auth): implement JWT-based authentication
```

### Bundling Scripts

If you notice the model would repeatedly need to write the same helper script when using the skill, bundle it. Write it once in `scripts/`, and reference it from SKILL.md. This saves every future invocation from reinventing the wheel.

### Improving an Existing Skill

When revising a skill:

1. **Generalize, don't overfit.** Skills get used across many different prompts. Rather than adding fiddly constraints for specific cases, try different metaphors or patterns of working.
2. **Re-apply the Style rules above** with fresh eyes — lean prompt, explained whys, imperative form.
3. **Preserve the original name.** Keep the directory name and `name` frontmatter field unchanged when updating.

### Audit Checklist

When the user asks to review, audit, or evaluate an existing skill, walk this checklist and report findings grouped by severity (Critical / Recommended / Accurate). Each item is independent — some may pass, some fail.

**Frontmatter integrity**
- `name:` matches the parent directory name exactly (or is omitted, relying on the directory name)
- `name:` is kebab-case, ≤64 chars, no uppercase/underscores
- Combined `description:` + `when_to_use:` ≤1,536 chars (truncation point in the skill listing)
- `description:` includes both *what* the skill does AND *when* to trigger it, with the key use case front-loaded
- Trigger phrases are present — either inline in `description:` or split into `when_to_use:`
- `allowed-tools:` is a space- or comma-separated string or YAML list — this repo standardizes on YAML arrays (`[Read, Edit]`); declared when the skill calls specific tools, with no broad grants the body never uses
- No stale fields: `compatibility`, `license`, `metadata` are not part of the current schema — remove if present

**Invocation discipline**
- If the skill must be manual-only, `disable-model-invocation: true` is set in frontmatter — this is the only hard guarantee
- **Misalignment check (Critical)**: a `description:` that self-declares manual-only ("Only invoke when the user explicitly requests it", "Never trigger autonomously") without the flag is a bug, not a stylistic choice — description-only guards waste the per-session budget *and* fail to prevent auto-invocation. Resolve in the right direction: set the flag if nothing needs Claude to invoke the skill; if something does (workflow step, proactive suggestion, chaining), rewrite the description instead — see Repo Conventions for the two exceptions
- If the skill is proactive, description uses pushy language ("ALSO use proactively when…") with concrete detection cues
- If the skill should scope to specific file types, `paths:` globs are set rather than relying on description matching
- `user-invocable: false` is set only for background-knowledge skills the user shouldn't type directly (note: this controls `/` menu visibility, not Skill-tool access — use `disable-model-invocation: true` to actually block model invocation)

**Body quality**
- SKILL.md body under 500 lines; if longer, detail has been moved to `references/` with clear pointers
- Instructions use imperative form ("Read the file") not hedged ("You should read…")
- Heavy-handed MUST/ALWAYS/NEVER only where truly non-negotiable; elsewhere the *why* is explained
- No narrow overfitting to a single example — instructions generalize to similar cases
- Examples provided for any non-trivial output format

**Progressive disclosure**
- Metadata (name + description) conveys purpose without loading the body
- Bundled resources (`scripts/`, `references/`, `assets/`) used when content is large, repetitive, or rarely needed
- Reference files >100 lines include a table of contents

**Project fit**
- Matches conventions of sibling skills in the same repo (naming prefix, subagent model, tool usage)
- Aligns with any repo-level CLAUDE.md directives that constrain skill behavior
- Does not duplicate an existing skill's trigger surface

**Report format**
Group findings as: Critical (breaks discovery or invocation), Recommended (quality/consistency), Accurate (passes — list briefly so the user sees what's already good). For each Critical/Recommended item, cite the line number and propose a concrete fix.

