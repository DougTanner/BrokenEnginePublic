---
name: codex-review
description: >-
  Claude Code fallback for when Fable is unavailable or its limit is reached: run a
  delegated reviewer/auditor role such as /plan-audit, /repo-code-review,
  or /session-audit on Codex/Sol headless via `codex exec`, and return
  its output in the target role's format. If Codex is also unavailable it reports
  `CODEX-UNAVAILABLE: reason` so the caller uses Opus instead. Invoked per the
  global model-fallback rule in AGENTS.md. Not for Codex — under the Fable→Sol
  mapping Codex is already Sol, so it never calls this.
allowed-tools: [Read, Bash]
---

# Codex Review (Fable-unavailable fallback → Sol)

Run a delegated reviewer/auditor role on **Codex/Sol** headless when Fable can't serve it. You are a thin driver: Codex performs the role using the same inputs and contract a Fable subagent would receive.

## Inputs (from caller)
- `targetSkill` — the role's skill (e.g. `plan-audit`, `repo-code-review`, `session-audit`)
- That role's normal inputs: changed-file list, touched regions, plan/intent, residuals/focus areas
- Worktree path and changed-file baseline commit (derive if absent: worktree = current repo root; baseline = `HEAD`, because process reviews precede the step 13 commit)

## Method
1. Assemble the review target into a scratch file (session scratchpad): for a code review, start with `git -C <worktree> diff <baseline> -- <changed files>`, then use `git -C <worktree> ls-files --others --exclude-standard -- <changed files>` and append each returned path plus its full contents. Never omit new files. For a plan/doc audit, include the file(s) the caller names.
2. Write the prompt to a scratch file. Tell Codex to read and follow `<worktree>/.agents/skills/<targetSkill>/SKILL.md`, review only the supplied target, and emit the target skill's output template followed by the AGENTS.md residuals footer.
3. Run the helper:
   `pwsh -File <worktree>/.codex/codex-review.ps1 -Worktree <worktree> -PromptFile <prompt> -OutFile <out>`
   (Codex runs `gpt-5.6-sol` headless with bypass at xhigh; ~1–3 min. Don't tail its stdout.)
4. Read `<out>`; verify it satisfies the target skill's output template + residuals footer, coercing shape only (never invent findings). Return that block verbatim as your entire output.

## Fallback (never block the process)
If the helper exits non-zero (127 = codex missing), `<out>` is empty/garbled, or the diff step fails, return `CODEX-UNAVAILABLE: <short reason>` as the first line, followed only by the mandatory AGENTS.md residuals footer. The caller recognizes the first-line sentinel and runs the role on Opus. Do not retry Codex more than once.

## Notes
- Findings only — never edit code (the target roles are findings-only; Codex runs read-only by instruction even though the sandbox is bypassed).
- Billing: Codex bills the ChatGPT subscription, not metered API credits. Keep `OPENAI_API_KEY` out of your environment (see `.codex/codex-review.ps1`).
