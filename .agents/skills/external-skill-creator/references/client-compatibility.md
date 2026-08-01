# Client Compatibility

Read this reference when a skill adds or changes client-specific metadata, prompt substitutions, interaction tools, delegation, or invocation policy. Keep the main `SKILL.md` workflow portable and place client-only mechanics here or in another focused reference.

## Shared Package

Both clients consume the skill directory and `SKILL.md`, but their policy surfaces are independent. Write portable instructions in ordinary language, then configure every supported client explicitly. Never infer invocation behavior from an `external-` name.

Repository frontmatter follows `../../validate-skill/references/frontmatter-schema.md`. The repository validator, not a client installation, owns its accepted fields and relationships.

## Claude Code

Claude Code reads the repository frontmatter controls. For a genuinely user-only skill, set `disable-model-invocation: true`; this prevents automatic use and native skill-to-skill invocation. Omit it when a documented workflow must invoke the skill, and make the description state the exact automatic or chaining contexts. This skill remains Claude-manual-only.

Users invoke a listed Claude skill explicitly as `/skill-name`; `.claude/skills` exposes the repository package.

The `external-architecture-review` and `external-refactor-clean` skills are native-chain exceptions: they may be invoked by an explicitly documented parent workflow even though ordinary implicit matching stays disabled. Model the exception with client policy, not the `external-` prefix.

Claude-only prompt features belong in a compatibility reference:

- `$ARGUMENTS` is the full invocation argument string; `$ARGUMENTS[N]` and `$N` select zero-based positional arguments. If the body does not mention `$ARGUMENTS`, Claude Code appends supplied arguments to the prompt.
- `${CLAUDE_SESSION_ID}`, `${CLAUDE_SKILL_DIR}`, and `${CLAUDE_EFFORT}` expose session, package-directory, and effort values.
- Inline shell injection uses a backtick-delimited command prefixed by `!`; its output replaces the placeholder before the prompt loads. The multiline form uses a fenced block whose info string is `!`. Describe that form in prose inside skills because a literal multiline opener can be executed by the loader. Managed policy can disable this feature.
- `AskUserQuestion` is Claude's structured choice prompt, and `Agent` is its delegation tool. State the human interaction or delegation outcome in shared instructions; name these tools only in Claude-specific guidance.
- The word `ultrathink` enables Claude Code extended thinking. Do not include it in portable instructions unless that client behavior is intentional.

Use `allowed-tools` only for tools the workflow actually needs.

## Codex

Codex reads optional client metadata from `agents/openai.yaml`. Invocation policy lives under `policy`, independently of Claude frontmatter, and follows the schema linked above. The minimal file is:

```yaml
policy:
  allow_implicit_invocation: false
```

Codex does not interpret Claude substitutions, shell injection, `AskUserQuestion`, `Agent`, or the Claude extended-thinking keyword as portable skill behavior. Express the intended outcome in the shared workflow and use Codex-native interaction, collaboration, or shell mechanisms at execution time.

## Cross-Client Check

Before claiming dual-client support, verify:

1. shared instructions do not require one client's syntax;
2. Claude frontmatter matches Claude automatic, manual, and chaining behavior;
3. `agents/openai.yaml` matches Codex implicit and explicit behavior;
4. every referenced resource exists and is linked from `SKILL.md` when needed;
5. the repository `validate-skill` workflow passes for frontmatter and bundled links.
