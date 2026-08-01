---
name: validate-skill
description: Validate repository skill packages with the authoritative Claude and Codex mechanical and semantic contracts. Use after creating, revising, auditing, or final-tree verifying any `.agents/skills/*/SKILL.md`, and whenever frontmatter, `agents/openai.yaml`, invocation policy, trigger quality, bundled links, or progressive disclosure need review.
allowed-tools: [Read, Grep, Glob, PowerShell]
---

# Validate Skill

Run inside one fresh delegated `reviewer`; never delegate. Validate skills
without modifying them. Mechanical failures and Critical semantic findings
block a passing result; Recommended findings remain advisory.

## Inputs

Accept one repository skill directory or its `SKILL.md`. The optional Codex `agents/openai.yaml` is validated with the package. Disposable fixtures may be outside `.agents/skills/` only with `-Fixture`.

Read `references/frontmatter-schema.md` completely before interpreting frontmatter or a mechanical diagnostic. It is the authoritative repository schema; do not substitute a client-installed validator or a reduced prose check.

## Workflow

1. Bootstrap the validation boundary by running the mechanical command against this `validate-skill` directory:

   ```powershell
   pwsh -NoProfile -File .agents/skills/validate-skill/scripts/Validate-Skill.ps1 -Path .agents/skills/validate-skill
   ```

   Require `VALID` and exit `0`. Report `BLOCKED` if the command cannot run, returns `SETUP_ERROR`/2, or the validator does not validate its own skill. Do not continue with a weaker check.
2. Run the same command once for the target. Use `-Fixture` only for a deliberately disposable fixture outside `.agents/skills/`. Capture the exact command, exit status, and complete output.
3. Treat `INVALID`/1 as a mechanical Critical finding. Treat `SETUP_ERROR`/2, an unrecognized result class, or a result/exit mismatch as `BLOCKED`.
4. Review semantic behavior from `SKILL.md`, directly referenced resources, and a present Codex companion file. Collect inbound references by running `pwsh -NoProfile -File <repository root>/.agents/skills/validate-skill/scripts/Find-SkillInboundReferences.ps1 -SkillName <name>`, which sweeps the documented root set and returns capped `{path, line, text}` records with per-root hit counts; never reconstruct that sweep inline. Complete a `truncated` result with targeted searches of the roots whose reported counts exceed the returned records. Report the blocker on error (exit `1`). Classify surrounding text: model invocation/chaining is a workflow requirement; a command only the user types is an example.
   - Treat Claude `disable-model-invocation` and Codex `policy.allow_implicit_invocation` as independent controls. Never require a Codex companion file merely because the Claude flag is true. A platform-neutral or Codex-specific manual-only promise requires Codex policy `false`; a Claude-only promise requires the Claude flag. A disabled policy that conflicts with an inbound workflow requirement is Critical for that client.
   - Critical: `description` lacks meaningful trigger contexts. Codex discovery sees `name` and `description`, not `when_to_use` or the body.
   - Recommended: remove unused Claude pre-approvals and add those needed by prescribed commands. Keep body tool and delegation bounds authoritative; Codex dependencies wire external tools and grant no execution authority.
   - Recommended: keep instructions imperative, general, and concise; include an example for a non-trivial required output format.
   - Recommended: plain wording — apply the one-term-per-concept and plain-words rules in root `AGENTS.md` `## Directives`, and name concrete actions instead of abstract noun-stacks unless the stack is an established term or code identifier. Report a reintroduced synonym for an established term as a finding.
   - Recommended: phrase each rule positively as the action to take; keep a prohibition only where the requirement cannot be stated positively, and pair it with what to do instead.
   - Recommended: end each workflow step on a checkable done-condition, so the agent can tell done from not-done.
   - Recommended: measure large bodies with `.agents/scripts/Measure-Tokens.ps1`; consider progressive disclosure above 10,000 `bt-token-v1`, target at most 15,000, and give reference files over 2,000 a table of contents.
5. List confirmed passes under Accurate checks. Do not promote ordinary quality advice to Critical unless discovery or invocation is concretely incorrect.

For validator changes, run the disposable `VALID`/`INVALID`/`SETUP_ERROR` matrix in the schema reference. Do not commit fixture packages.

## Output

Return:

```markdown
Validation: PASS | FAIL | BLOCKED
Mechanical evidence:
- command, exit, decisive output
Critical findings:
- `path:line` — finding and concrete correction
- none
Recommended findings:
- `path:line` — advisory improvement
- none
Accurate checks:
- confirmed check
```

Use `PASS` only when both mechanical runs succeed and no Critical finding remains. Use `FAIL` for target content or semantic Critical findings. Use `BLOCKED` for setup, invocation, read, or internal-validator failures.
