---
name: skill-creator
description: Create new skills and improve existing skills following best practices. Use when users want to create a skill from scratch, edit or revise an existing skill, review a skill for quality, or need guidance on skill structure, frontmatter, descriptions, writing patterns, or progressive disclosure.
---

# Skill Creator

A guide for creating and improving Claude Code skills following best practices.

Your job is to help the user create or improve a skill. Understand what they want the skill to do, then draft or revise the SKILL.md following the best practices below. Be flexible — some users want to iterate collaboratively, others just want a quick draft.

## Creating a Skill

### Capture Intent

Start by understanding the user's intent. The current conversation might already contain a workflow the user wants to capture (e.g., they say "turn this into a skill"). If so, extract answers from the conversation history first — the tools used, the sequence of steps, corrections the user made, input/output formats observed. The user may need to fill the gaps, and should confirm before proceeding to the next step.

1. What should this skill enable Claude to do?
2. When should this skill trigger? (what user phrases/contexts)
3. What's the expected output format?

### Interview and Research

Proactively ask questions about edge cases, input/output formats, example files, success criteria, and dependencies.

Check available MCPs - if useful for research (searching docs, finding similar skills, looking up best practices), research in parallel via subagents if available, otherwise inline. Come prepared with context to reduce burden on the user.

### Write the SKILL.md

Based on the user interview, fill in these components:

- **name**: Skill identifier (kebab-case, max 64 characters)
- **description**: When to trigger, what it does (see Description Best Practices below)
- **compatibility**: Required tools, dependencies (optional, rarely needed)
- **the body**: The actual skill instructions

---

## Skill Structure

### Anatomy of a Skill

```
skill-name/
├── SKILL.md (required)
│   ├── YAML frontmatter (name, description required)
│   └── Markdown instructions
└── Bundled Resources (optional)
    ├── scripts/    - Executable code for deterministic/repetitive tasks
    ├── references/ - Docs loaded into context as needed
    └── assets/     - Files used in output (templates, icons, fonts)
```

### Frontmatter

Required fields in YAML frontmatter:
- `name`: kebab-case identifier (lowercase letters, digits, hyphens; max 64 chars)
- `description`: What the skill does and when to use it (max 1024 chars, no angle brackets)

Optional fields:
- `compatibility`: Required tools or dependencies (max 500 chars)
- `license`: License identifier
- `allowed-tools`: Tools the skill needs access to
- `metadata`: Additional metadata

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
- Maximum 1024 characters (descriptions over this are truncated)
- Aim for 100-200 words — concise but comprehensive
- No angle brackets (< or >)

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

### Security

Skills must not contain malware, exploit code, or content that could compromise system security. A skill's contents should not surprise the user in their intent if described.

