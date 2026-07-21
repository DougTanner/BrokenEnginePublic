---
name: codex-review
description: >-
  Claude Code manual-only fallback when Fable is unavailable: runs one assigned
  reviewer or auditor role on Codex/Sol headless. Codex callers stop because
  they are already the Sol mapping.
allowed-tools: [Read, Bash]
disable-model-invocation: true
---

# Codex Review (Fable-unavailable fallback)

Claude Code may run one delegated reviewer or auditor role on Codex/Sol
headless only when the normal Fable role is unavailable. This preserves role
capability; it does not create a second opinion or a provenance chain. Codex
callers must stop instead of invoking this skill recursively.

## Inputs

- `targetSkill` — the assigned role, such as `plan-audit`, `repo-code-review`,
  or `session-audit`
- Its normal target: plan/intent, changed files and regions, and current
  residuals or reviewer focus
- Worktree and baseline (default to current repository root and `HEAD`)

## Method

1. Assemble only the target into a session scratch file. For code review, use
   `git -C <worktree> diff <baseline> -- <changed files>` and append the full
   contents of any named untracked files.
2. Prompt Codex to read the selected skill, review only that target, and return
   the selected role's normal concise inline handoff. It must not edit files.
3. Run `pwsh -File <worktree>/.codex/codex-review.ps1 -Worktree <worktree>
   -PromptFile <prompt> -OutFile <out>`.
4. Read `<out>` and return it verbatim. Do not reshape findings or invent
   evidence.

## Fallback

If the helper fails or returns unusable output, return
`CODEX-UNAVAILABLE: <short reason>` with the unchanged target and residual.
Claude Code may retry the same assignment at most once using
`subagent_type: "general-purpose"` with `model: "opus"` and the reviewer or
auditor role stated at the top of the prompt. Do not retry Codex or add another
reviewer for consensus.

## Notes

- Findings only; never edit code.
- A final-evidence gate records the final result once if one is active.
