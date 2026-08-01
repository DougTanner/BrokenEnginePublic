---
name: codex-review
description: >-
  Primary Claude Code route running any delegated reviewer or auditor role on
  Codex/Sol headless. Codex callers stop because they are already the Sol
  mapping (see the root AGENTS.md role table). Use for every Change Workflow review dispatch in Claude Code.
allowed-tools: [Read, Bash]
---

# Codex Review

Claude Code runs every delegated reviewer or auditor role (Sol on Codex, per the
root [AGENTS.md](../../../AGENTS.md) role table) — `plan-audit`,
`repo-code-review`, `glsl-review`, `scope-review`, `adversarial-review`,
`session-audit`, and external review lenses — on Codex/Sol through this skill.
Parent/manager orchestrators that dispatch their own child reviewer, including manually
triggered `/next-plan-review`, are excluded. A `/codex-review` invocation of an assigned skill constitutes the
delegated-`reviewer` execution context; it is not an "inline run" in the assigned
skills' vocabulary, and `/verify-changes` records it as the delegated reviewer
execution. Codex callers must stop instead of invoking this skill recursively — their reviewer
role already resolves to Sol.

## Inputs

- Assigned skill — the reviewer or auditor role to run, such as `plan-audit`, `repo-code-review`,
  or `session-audit`
- Its normal inputs: plan/intent, changed files and regions, and current
  residuals or reviewer focus
- Worktree and session baseline (default to current repository root and `HEAD`)

## Method

1. Assemble the prompt with
   [scripts/New-CodexReviewPrompt.ps1](scripts/New-CodexReviewPrompt.ps1), which
   writes the prompt file and returns only a small receipt, so the diff never
   enters this session. NEVER reconstruct the prompt assembly, the guardrail
   block, or the evidence collection inline, and never paste diff bytes into the
   session. The fixed wording lives in
   [references/prompt-template.md](references/prompt-template.md) and is changed
   only there. The manager runs the script before dispatch; the reviewer, inside
   the Codex `--sandbox read-only` environment, only reads the prompt file it
   wrote. In Codex's PowerShell 7 terminal:

   ```powershell
   pwsh -NoProfile -ExecutionPolicy Bypass -File <worktree>/.agents/skills/codex-review/scripts/New-CodexReviewPrompt.ps1 -RepositoryRoot <worktree> -Baseline <full 40-character baseline SHA> -AssignedSkill <assigned skill> -ScopeFile <scope file> -PromptPath <new prompt path> [-RiskTier <1|2|3>] [-UntrackedPath <comma-separated paths>] [-Head <rev>]
   ```

   In Claude Code's Git Bash terminal, convert the script path first:

   ```bash
   repository_root="$(git rev-parse --show-toplevel)"
   script="$(cygpath -w "$repository_root/.agents/skills/codex-review/scripts/New-CodexReviewPrompt.ps1")"
   pwsh -NoProfile -ExecutionPolicy Bypass -File "$script" -RepositoryRoot "$repository_root" -Baseline <baseline> -AssignedSkill <assigned skill> -ScopeFile <scope file> -PromptPath <new prompt path>
   ```

   Write the judgment content yourself into `-ScopeFile`: the exact scope, the
   files and regions authorized for review, focus notes, and current residuals.
   The script copies that text verbatim and never authors, summarizes, or edits
   it, and never decides which files are in scope. For a reviewer role with no
   skill file — the Tier-2 coherence review, for one — pass a descriptive role
   name as `-AssignedSkill` and put that role's full review contract in
   `-ScopeFile`. `-RiskTier` adds one
   `Risk tier: <n>` line above that text. `-PromptPath` must not already exist;
   `-UntrackedPath` names every untracked file the review needs, and does not
   combine with `-Head`.
2. Read the receipt, one compact JSON object on stdout. Exit `0` carries
   `promptPath`, `promptBytes`, `fileCount`, `binaryExcluded`, and
   `sectionsWritten`. Exit `2` is blocked and its `code` names the fix:
   `prompt.path-exists` — choose an unused prompt path, the existing file is left
   untouched; `prompt.diff-too-large` — the evidence passed the 4 MB budget, so
   split the review into smaller authorized scopes and never truncate the
   evidence; `prompt.inventory-truncated` — name every untracked path or narrow
   the baseline until the evidence is complete; `prompt.untracked-path-unknown` —
   a named path is not an untracked file; `prompt.head-untracked-conflict` — a
   commit-valued head has no untracked side. Exit `1` is a script error: stop and
   report its `code` and `message` rather than hand-assembling a prompt.
3. Run `pwsh -File <worktree>/.codex/codex-review.ps1 -Worktree <worktree>
   -PromptFile <the receipt's promptPath> -OutFile <out>` with the maximum tool timeout (10
   minutes). For a large diff or Tier-3 scope, run in background and wait for
   completion rather than truncating.
4. Read `<out>`; success requires a non-empty structured result — a skill-native
   status line or a verdict token, either vocabulary counts. Benign CLI
   notices/deprecations are not failures. Return the handoff plus the `<out>`
   path; the output file is the retained full critique — do not paste
   extra narration beyond the concise handoff into the session.

## Manager evaluation

Sol over-reports edge cases and tends toward over-engineering, so the calling
manager session decides each finding under the root
[AGENTS.md](../../../AGENTS.md) decide-once and reject-speculative-findings
rules. Interruption findings — power loss, process kill, crash or timeout
mid-operation — are answered by the existing ordered fallback steps (idempotent
re-run, lease expiry, claim healing, Git state), never by new recovery
machinery; accept one only when a named interruption point provably defeats
those steps, and even then present the cost to the user before implementing.

## Fallback

If the helper fails or returns unusable output, stop and report
`CODEX-UNAVAILABLE: <short reason>` with the unchanged target. This is a
blocking failure: never dispatch a substitute reviewer automatically. Only
explicit user authorization in the current session unblocks it, by routing the
same unchanged assignment to the normal Opus `reviewer` subagent — or, if that
subagent type is also unavailable, at most once to `subagent_type:
"general-purpose"` with `model: "opus"` and the reviewer or auditor role stated
at the top of the prompt. Do not retry Codex or add another reviewer for
consensus.

## Notes

- Findings only; never edit code.
- An active landing gate records the final result once.
