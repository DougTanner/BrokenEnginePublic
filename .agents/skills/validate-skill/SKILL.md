---
name: validate-skill
description: Validate repository skills with the authoritative mechanical and semantic contract. Use after creating, revising, auditing, or final-tree verifying any `.agents/skills/*/SKILL.md`, and whenever skill frontmatter, invocation controls, trigger quality, tool grants, bundled links, or progressive disclosure need review.
allowed-tools: [Read, Grep, Glob, PowerShell]
---

# Validate Skill

Validate skills without modifying them. Mechanical failures and Critical semantic findings block a passing result; Recommended findings remain advisory.

## Inputs

Accept one repository skill directory or its `SKILL.md`. Disposable validation fixtures may be outside `.agents/skills/` only when the mechanical command uses `-Fixture`.

Read [`references/frontmatter-schema.md`](references/frontmatter-schema.md) completely before interpreting frontmatter or a mechanical diagnostic. It is the authoritative repository schema; do not substitute a client-installed validator or a reduced prose check.

## Workflow

1. Bootstrap the validation boundary by running the mechanical command against this `validate-skill` directory:

   ```powershell
   pwsh -NoProfile -File .agents/skills/validate-skill/scripts/Validate-Skill.ps1 -Path .agents/skills/validate-skill
   ```

   Require `VALID` and exit `0`. Report `BLOCKED` if the command cannot run, returns `SETUP_ERROR`/2, or the validator does not validate its own skill. Do not continue with a weaker check.
2. Run the same command once for the target. Use `-Fixture` only for a deliberately disposable fixture outside `.agents/skills/`. Capture the exact command, exit status, and complete output.
3. Treat `INVALID`/1 as a mechanical Critical finding. Treat `SETUP_ERROR`/2, an unrecognized result class, or a result/exit mismatch as `BLOCKED`.
4. Review semantic behavior from the target `SKILL.md` and its directly referenced resources. Also search every tracked repository `AGENTS.md` and `.agents/skills/*/SKILL.md` for inbound references to the target skill. Classify each match by surrounding workflow text: an instruction that the model invoke, chain to, or use the target programmatically is a workflow requirement; a command shown only for a user to type is a user-invocation example and does not conflict with disabled model invocation.
   - **Critical:** the description says the skill is manual-only or must never auto-trigger, but `disable-model-invocation: true` is absent.
   - **Critical:** `disable-model-invocation: true` is present, but an `AGENTS.md` or another skill requires the model to invoke or chain to it programmatically. Distinguish user-invocation examples from model workflow requirements.
   - **Recommended:** make the description state both what the skill does and concrete trigger contexts, with the key use case first.
   - **Recommended:** remove tool grants the workflow never uses, and declare grants needed by prescribed commands.
   - **Recommended:** keep instructions imperative, general, and concise; include an example for a non-trivial required output format.
   - **Recommended:** measure large bodies with `.agents/scripts/Measure-Tokens.ps1`; consider progressive disclosure above 10,000 `bt-token-v1`, target at most 15,000, and give reference files over 2,000 a table of contents.
5. List confirmed passes under Accurate checks. Do not promote ordinary quality advice to Critical unless discovery or invocation is concretely incorrect.

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
