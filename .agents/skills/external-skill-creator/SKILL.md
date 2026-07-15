---
name: external-skill-creator
description: Create new skills and improve existing skills following best practices. Use when users want to create a skill from scratch, edit or revise an existing skill, review a skill for quality, or need guidance on skill structure, frontmatter, descriptions, writing patterns, or progressive disclosure. User-invoked via /external-skill-creator (the disable-model-invocation flag means Claude cannot invoke or chain to this skill).
allowed-tools: [Read, Write, Edit, Glob, Grep, Agent, Bash, PowerShell]
disable-model-invocation: true
---

# Skill Creator

Help the user create or improve a skill: understand what it should do, then draft or revise SKILL.md following the practices below. Be flexible — some users iterate collaboratively, others want a quick draft.

## Repo Conventions (Broken Engine)

Sibling skills in this repo follow these conventions — match them when creating a new skill:

- **`external-` prefix** marks explicit-invocation skills (e.g., `external-grill-plan`, `external-design-interface`, `external-deep-analysis`). Most set `disable-model-invocation: true`, but the flag removes the skill from Claude's reach entirely (no auto-trigger, no Skill tool) — set it only when nothing, neither a documented workflow stage nor Claude itself, needs programmatic invocation. Two intentional exceptions are `external-grill-plan` (the main agent invokes it during the plan-approval stage of the AGENTS.md C++ Code Change Process) and `external-design-interface` (proactively suggests itself, which requires staying in the listing). Locally-created workflow skills such as `implement-plan`, `plan-audit`, and `finalize-changes` omit the prefix and the flag so the process can invoke them. User-only skills without the prefix (e.g., `gaea2-load`, `next-plan`) follow the same rule. Unprefixed proactive skills (e.g., `add-collection`, `repo-code-review`, `compile`) auto-trigger on matching contexts.
- **Directory layout**: each skill lives at `.agents/skills/<name>/SKILL.md` with optional `references/`, `scripts/`, `assets/` sidecars; `.claude/skills` exposes the same directory to Claude Code.
- **Frontmatter style**: `allowed-tools:` uses the schema's flow-list syntax, such as `[Read, Edit, Write]`.

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

Read [`../validate-skill/references/frontmatter-schema.md`](../validate-skill/references/frontmatter-schema.md) before writing frontmatter. It is the complete repository schema; do not infer fields from a client installation or add undocumented controls. Write the body using the practices below.

---

## Skill Structure

### Anatomy of a Skill

```
skill-name/
├── SKILL.md (required)
│   ├── YAML frontmatter (repository schema required)
│   └── Markdown instructions
└── Supporting files (optional — any layout; reference them from SKILL.md)
    ├── scripts/    - Executable code Claude runs via Bash (not loaded into context)
    ├── references/ - Docs loaded only when Claude reads them
    ├── examples/   - Sample outputs demonstrating expected format
    └── assets/     - Files used in output (templates, icons, fonts)
```

Claude Code skills follow the [Agent Skills](https://agentskills.io) open standard; the `scripts/` + `references/` + `assets/` layout is a convention from Anthropic's own skill-creator, not a requirement. Any file layout works as long as SKILL.md points at the files it needs.

### Frontmatter Contract

Use the shared `validate-skill` schema reference named above as the sole field, type, value, and relationship contract. Update that shared schema and validator together if the repository deliberately adopts a new control.

**String substitutions** (used inside the markdown body)
- `$ARGUMENTS` — the full argument string as typed. If the body omits `$ARGUMENTS` but the skill is invoked with arguments, Claude Code appends `ARGUMENTS: <value>` to the end of the skill content automatically, so user input is never silently dropped.
- `$ARGUMENTS[N]` or `$N` — positional argument by 0-based index (shell-quoted, so wrap multi-word values in quotes)
- `${CLAUDE_SESSION_ID}` — current session ID
- `${CLAUDE_SKILL_DIR}` — directory containing the skill's SKILL.md (for referencing bundled scripts)
- `${CLAUDE_EFFORT}` — current effort level

**Inline shell execution** — a backtick-bang-command-backtick span (backtick, `!`, command, backtick) runs before Claude sees the prompt; the output replaces the placeholder. Use for injecting live data (PR diffs, git status, environment info). A multi-line form also exists: a fenced block whose info-string is just `!` (three backticks immediately followed by `!`). The policy setting `"disableSkillShellExecution": true` (typically managed settings) replaces each command with a disabled-by-policy notice instead of running it.

> Note: do **not** include a literal backtick-backtick-backtick-bang sequence inside a SKILL.md (even quoted inside a code span) — the harness's shell-injection parser will match it before markdown parsing runs, try to execute the rest of the file as a shell block, and abort the skill load. Describe the syntax in prose instead of pasting it.

**Extended thinking** — including the word `ultrathink` anywhere in the body enables extended-thinking mode while the skill runs.

### Progressive Disclosure

Skills use a three-level loading system:
1. **Metadata** (name + description) - Always in context (~100 words)
2. **SKILL.md body** - Loads on invocation and **stays in context for the rest of the session** — its entire size is a recurring context cost
3. **Bundled resources** - Loaded as needed (unlimited size; scripts can execute without being loaded into context)

**Key patterns:**
- Measure with `pwsh -NoProfile -File .agents/scripts/Measure-Tokens.ps1 -Path <path>`; `bt-token-v1` is normalized UTF-8 bytes divided by four, rounded up, not an exact model-token count
- Consider progressive disclosure above 10,000 bt-token-v1 and target at most 15,000 bt-token-v1 for the SKILL.md body
- For reference files over 2,000 bt-token-v1, include a table of contents

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

### Validate Every Result

After every skill creation, revision, or audit, invoke `/validate-skill` on the finished repository skill. It owns the mechanical command, semantic review, severities, and report format. Fix every mechanical or Critical finding and rerun until it passes. Recommended findings remain advisory. A `BLOCKED` or setup failure stops validation; never substitute a delimiter check, client-installed validator, or abbreviated checklist.

