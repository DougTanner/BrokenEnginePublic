---
name: external-skill-creator
description: Create new skills and improve existing skills following best practices. Use when users want to create a skill from scratch, edit or revise an existing skill, review a skill for quality, or need guidance on skill structure, frontmatter, descriptions, writing patterns, or progressive disclosure. Invoked via /external-skill-creator or chained from skills that need to author/audit a sibling skill.
allowed-tools: [Read, Write, Edit, Glob, Grep, Agent]
disable-model-invocation: true
---

# Skill Creator

A guide for creating and improving Claude Code skills following best practices.

Your job is to help the user create or improve a skill. Understand what they want the skill to do, then draft or revise the SKILL.md following the best practices below. Be flexible — some users want to iterate collaboratively, others just want a quick draft.

## Repo Conventions (Broken Engine)

Sibling skills in this repo follow these conventions — match them when creating a new skill:

- **`external-` prefix** marks explicit-invocation-only skills (e.g., `external-grill-plan`, `external-design-interface`, `external-deep-analysis`). These **must** set `disable-model-invocation: true` in frontmatter — a description-level guard like "Only invoke when the user explicitly requests it" is *not* sufficient on its own (the description still loads into the auto-trigger listing and burns context budget). The same applies to user-only skills without the prefix (e.g., `gaea2-load`, `reduce-file`). Unprefixed proactive skills (e.g., `add-collection`, `code-review`, `compile`) auto-trigger on matching contexts.
- **Directory layout**: each skill lives at `.claude/skills/<name>/SKILL.md` with optional `references/`, `scripts/`, `assets/` sidecars.
- **Frontmatter style**: `allowed-tools:` uses YAML array syntax `[Read, Edit, Write]` in this repo (the skill schema also accepts space-separated strings, but the repo is consistent on arrays).

## Creating a Skill

### Capture Intent

Start by understanding the user's intent. The current conversation might already contain a workflow the user wants to capture (e.g., they say "turn this into a skill"). If so, extract answers from the conversation history first — the tools used, the sequence of steps, corrections the user made, input/output formats observed. The user may need to fill the gaps, and should confirm before proceeding to the next step.

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
- **Optional behavior fields**: `when_to_use`, `disable-model-invocation`, `user-invocable`, `allowed-tools`, `paths`, `argument-hint`, `context` + `agent`, `model`, `effort`, `hooks`, `shell` (see Frontmatter Reference below)
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
- `description`: What the skill does and when to use it. Combined with `when_to_use`, truncated at **1,536 characters** in the skill listing. Front-load the key use case.
- `when_to_use`: Additional trigger phrases or example requests. Appended to `description` in the listing; shares the 1,536-char cap.

**Invocation control**
- `disable-model-invocation: true` — Claude cannot auto-invoke; only the user can trigger with `/name` (chained programmatic invocation from another skill still works). Use for workflows with side effects (commits, deploys) or that must be user-initiated. When set, the description is **not loaded into context** — the budget cost is zero, so you can keep a long, human-readable description for `/help` and source-tree readers without competing for the per-session description budget.
- `user-invocable: false` — hide from the `/` menu. Use for background-knowledge skills Claude should load automatically but users shouldn't type by hand. **Note**: this controls menu visibility only, not Skill-tool access — Claude can still invoke the skill programmatically. To block programmatic invocation, use `disable-model-invocation: true`.
- `paths`: Comma-separated string or YAML list of glob patterns. When set, Claude auto-loads the skill only when working with matching files.
- `argument-hint`: Autocomplete hint, e.g. `[issue-number]`.

**Execution environment**
- `allowed-tools`: **Space-separated string** or YAML list. Grants per-use approval for the listed tools while the skill is active. Supports fine-grained rules like `Bash(git add *)`.
- `context: fork` — run the skill in a forked subagent. Pair with `agent: Explore | Plan | general-purpose | <custom>`.
- `model`: Override the session model for this skill.
- `effort`: `low | medium | high | xhigh | max` (available levels depend on model).
- `hooks`: Skill-scoped lifecycle hooks.
- `shell`: `bash` (default) or `powershell` — controls the shell for `` !`command` `` injection.

**String substitutions** (used inside the markdown body)
- `$ARGUMENTS` — the full argument string as typed. If the body omits `$ARGUMENTS` but the skill is invoked with arguments, Claude Code appends `ARGUMENTS: <value>` to the end of the skill content automatically, so user input is never silently dropped.
- `$ARGUMENTS[N]` or `$N` — positional argument by 0-based index (shell-quoted, so wrap multi-word values in quotes)
- `${CLAUDE_SESSION_ID}` — current session ID
- `${CLAUDE_SKILL_DIR}` — directory containing the skill's SKILL.md (for referencing bundled scripts)

**Inline shell execution** — a backtick-bang-command-backtick span (backtick, `!`, command, backtick) runs before Claude sees the prompt; the output replaces the placeholder. Use for injecting live data (PR diffs, git status, environment info). A multi-line form also exists: open a fenced block whose info-string is just `!` (three backticks immediately followed by `!`), put commands inside, and close with three backticks. This behavior can be disabled at the policy level by setting `"disableSkillShellExecution": true` in settings — when set, each command is replaced with `[shell command execution disabled by policy]` instead of running. Most useful in managed settings where users can't override it.

> Note: do **not** include a literal backtick-backtick-backtick-bang sequence inside a SKILL.md (even quoted inside a code span) — the harness's shell-injection parser will match it before markdown parsing runs, try to execute the rest of the file as a shell block, and abort the skill load. Describe the syntax in prose instead of pasting it.

**Extended thinking** — including the word `ultrathink` anywhere in the body enables extended-thinking mode while the skill runs.

### Progressive Disclosure

Skills use a three-level loading system:
1. **Metadata** (name + description) - Always in context (~100 words)
2. **SKILL.md body** - In context whenever skill triggers (<500 lines ideal)
3. **Bundled resources** - Loaded as needed (unlimited size; scripts can execute without being loaded into context)

**Key patterns:**
- Keep SKILL.md under 500 lines; if approaching this limit, move detail into reference files with clear pointers about when to read them
- For large reference files (>300 lines), include a table of contents

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

The description field is the primary mechanism that determines whether Claude invokes a skill. It appears in Claude's `available_skills` list, and Claude decides whether to consult a skill based solely on the name and description.

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

**Prefer the frontmatter mechanism over prompt-level guards.** If a skill should only be invoked manually, set `disable-model-invocation: true` in frontmatter — this is a hard guarantee, not a prompt hedge. Description language like "Never trigger autonomously" is **not a substitute**: the description still loads, still consumes the per-session budget, and Claude can still match against it. Treat the two as decoupled: the flag controls invocation, the description is for human readers.

A frequent misconfiguration in this repo's history was descriptions that self-declared manual-only ("Only invoke when the user explicitly requests it") without the flag set — wasting context budget and failing to guarantee non-invocation. When auditing or revising, treat that combination as a Critical finding.

The natural-language-discovery loss is real but usually intentional: skills that genuinely need to be user-invoked (because they have side effects, are expensive, or must be explicit) shouldn't be auto-triggered by phrase-matching anyway. If you find yourself wanting both auto-trigger AND manual-only behavior, the requirement is contradictory — pick one.

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
2. **Keep the prompt lean.** Remove instructions that aren't pulling their weight or that cause unproductive work.
3. **Explain the why.** Transmit understanding of *why* something matters into the instructions, rather than rigid rules.
4. **Preserve the original name.** Keep the directory name and `name` frontmatter field unchanged when updating.

### Audit Checklist

When the user asks to review, audit, or evaluate an existing skill, walk this checklist and report findings grouped by severity (Critical / Recommended / Accurate). Each item is independent — some may pass, some fail.

**Frontmatter integrity**
- `name:` matches the parent directory name exactly (or is omitted, relying on the directory name)
- `name:` is kebab-case, ≤64 chars, no uppercase/underscores
- Combined `description:` + `when_to_use:` ≤1,536 chars (truncation point in the skill listing)
- `description:` includes both *what* the skill does AND *when* to trigger it, with the key use case front-loaded
- Trigger phrases are present — either inline in `description:` or split into `when_to_use:`
- `allowed-tools:` uses **space-separated** string (not commas) or YAML list; declared when the skill calls specific tools
- No stale fields: `compatibility`, `license`, `metadata` are not part of the current schema — remove if present

**Invocation discipline**
- If the skill must be manual-only, `disable-model-invocation: true` is set in frontmatter — this is the only hard guarantee
- **Misalignment check (Critical)**: if `description:` self-declares manual-only ("Only invoke when the user explicitly requests it", "Never trigger autonomously", "/external-…"), `disable-model-invocation: true` **must** also be set. Description-only guards waste the per-session budget *and* fail to actually prevent auto-invocation. Search the listing for skills whose description claims manual-only but lack the flag — that is a bug, not a stylistic choice
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
- Reference files >300 lines include a table of contents

**Project fit**
- Matches conventions of sibling skills in the same repo (naming prefix, subagent model, tool usage)
- Aligns with any repo-level CLAUDE.md directives that constrain skill behavior
- Does not duplicate an existing skill's trigger surface

**Report format**
Group findings as: Critical (breaks discovery or invocation), Recommended (quality/consistency), Accurate (passes — list briefly so the user sees what's already good). For each Critical/Recommended item, cite the line number and propose a concrete fix.

### Security

Skills must not contain malware, exploit code, or content that could compromise system security. A skill's contents should not surprise the user in their intent if described.

