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

1. Assemble one scratch prompt file with sections separated by a `---` delimiter
   line:
   - (a) role instruction naming the assigned skill to read and execute
   - (b) exact scope — files/regions and risk tier
   - (c) evidence — `git -C <worktree> diff <baseline> -- <changed files>` plus
     the full contents of any named untracked text files; represent a binary
     untracked entry by path and status only, never by its contents
   - (d) the required output contract
2. Embed these guardrails as explicit prohibition lines in the prompt: a finding
   is actionable only if it names a concrete reachable failure; NEVER propose
   speculative refactors, abstractions, defensive validation, or scope beyond the
   changed bytes and the assigned skill's remit; NEVER edit files; NEVER load a
   screenshot, capture, image, or other binary payload into the review context
   unless the assigned skill's remit is the runtime criterion that payload settles
   — rely on the harness role's reported verdict and cited path. Output contract:
   return the assigned skill's normal concise handoff, then append one final line
   with a verdict token — `PASS`, `CHANGES-REQUIRED: <n>`, or `BLOCKED: <reason>`.
   The token supplements the skill-native status vocabulary (such as
   `NEEDS_ACTION`); it never replaces the skill's format.
3. Run `pwsh -File <worktree>/.codex/codex-review.ps1 -Worktree <worktree>
   -PromptFile <prompt> -OutFile <out>` with the maximum tool timeout (10
   minutes). For a large diff or Tier-3 scope, run in background and wait for
   completion rather than truncating.
4. Read `<out>`; success requires a non-empty structured result — a skill-native
   status line or a verdict token, either vocabulary counts. Benign CLI
   notices/deprecations are not failures. Return the handoff plus the `<out>`
   path; the output file is the retained full critique — do not paste
   extra narration beyond the concise handoff into the session.

## Manager evaluation

Sol over-reports edge cases and tends toward over-engineering. The calling
manager session adjudicates each finding for concrete reachable failure and materiality
before acting — speculative, unreachable, or gold-plating findings are rejected,
not fixed. Interruption findings — power loss, process kill, crash or timeout
mid-operation — are answered by the existing recovery ladder (idempotent
re-run, lease expiry, claim healing, Git state), never by new recovery
machinery; accept one only when a named interruption point provably defeats
that ladder, and even then present the cost to the user before implementing.
The existing adjudicate-once rule applies; this route adds no extra review
rounds.

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
