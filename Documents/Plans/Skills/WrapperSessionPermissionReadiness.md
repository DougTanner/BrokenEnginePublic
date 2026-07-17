# Wrapper Session Permission Readiness

## Context

A next-plan-review postmortem of commit e9c9d31c (rehashed as e1efba9b after a message reword) proved the executing wrapper session had Agent, Bash, and initially Edit tool permissions DENIED by the host: its single `/plan-audit` subagent spawn was refused, so it ran `/plan-audit`, `/repo-code-review`, `/code-style-review`, and all implementation inline in one main context that peaked at ~397k tokens, and it lost 35 minutes to an Edit-denied stall discovered only when the user returned. The C++ Change Workflow mandates subagent delegation and fresh-context reviews; host permissions silently defeated both.

Findings originate from a second-pass multi-agent review and are unverified claims; the executing agent must confirm each one before editing.

The denial is *not* explained by missing bypass flags — both wrappers already request full bypass: the `-ClientArguments "--dangerously-skip-permissions"` argument in `.claude/claude-worktree.sh` (`:15`) and `-ClientArguments '--dangerously-bypass-approvals-and-sandbox'` in the `Invoke-CodexWorktree` function of `.codex/codex-worktree.ps1` (`:14`). Something made the bypass ineffective for that session. Relevant verified state:

- `Start-AgentWorktreeSession.ps1` launches the client in a worktree under `$HOME\<client>\worktrees\<repo>\<uuid>` (`:34-35`, `:78`) — outside the primary checkout the user originally trusted; host trust/permission state may not carry to that directory.
- The project `.claude/settings.json` (tracked, so present in every worktree) contains only `worktree.baseRef` — no `permissions` block. `.claude/settings.local.json` carries a small allow list but is untracked, so it does not exist in wrapper worktrees. No wrapper-written per-session settings mechanism exists.
- The `## Preconditions` section of `.agents/skills/next-plan/SKILL.md` (`:19-34`) checks worktree/claim/baseline state but never probes tool permissions; its frontmatter `allowed-tools` (`:6`) lists `Agent` and `Edit`, which the host can still deny.

## Design

1. **Verify first** (per Diagnosis Discipline). Reproduce or directly evidence each claimed denial mode before changing anything: launch a wrapper session and observe whether Agent spawn and Edit are permitted, and identify *why* the existing bypass flags did not hold in the postmortem session (untrusted worktree directory, bypass disabled by policy, startup confirmation declined, user/managed settings override, or client version behavior). Refuted claims are dropped as named residuals — no speculative fixes or substitutes.

2. **Pre-authorize the tools wrapper sessions need** (Agent, Edit, Write, shell tools) through whichever mechanism step 1 proves Claude Code / Codex actually honors for these session types: wrapper launch configuration (flags or environment in `.claude/claude-worktree.sh` / `.codex/codex-worktree.ps1` / `Start-AgentWorktreeSession.ps1`) or a checked-in project permissions surface (a `permissions.allow` block in the tracked `.claude/settings.json`, which propagates into every worktree; the untracked `settings.local.json` does not). Investigate the actual permission mechanism first; do not guess.

3. **Fail-fast readiness probe** in the `## Preconditions` section of `.agents/skills/next-plan/SKILL.md`: before the plan-approval gate, verify an Agent spawn and an Edit would be permitted; on denial, abort with remedy text naming the misconfiguration (which setting/flag to fix, where) instead of silently proceeding inline.

4. **Delegation-denied fallback rule** in the workflow text: if Agent is denied mid-session after the probe passed, surface it to the user immediately and record "review freshness degraded" as an explicit residual in the acceptance ledger, rather than silently inlining reviews in the main context.

## Critical files

- `.claude/claude-worktree.sh` — Claude wrapper entry; existing `--dangerously-skip-permissions` argument (`:15`)
- `.codex/codex-worktree.ps1` — Codex wrapper entry; existing `--dangerously-bypass-approvals-and-sandbox` argument in `Invoke-CodexWorktree` (`:14`)
- `.agents/scripts/Start-AgentWorktreeSession.ps1` — shared session bootstrap; worktree location and client launch (`:34-35`, `:78`) if launch-side authorization is the honored mechanism
- `.claude/settings.json` — tracked project permission surface (currently no `permissions` block) if a settings-side grant is the honored mechanism
- `.agents/skills/next-plan/SKILL.md` — `## Preconditions` (`:19-34`) gains the readiness probe; workflow text gains the fallback rule

## Out of scope

- Any C++ change — this is agent scripts and skill text only.
- Broad permission grants beyond wrapper worktree sessions (do not widen the user's global or ordinary-checkout permission posture).
- The verify-changes ledger format — owned by `Documents/Plans/Skills/VerificationLedgerReviewRows.md` if both are in flight; warning-only overlap, note the residual line's location there rather than restructuring the ledger here.

## Acceptance criteria

- Every executed design item has a recorded verification result (confirmed/refuted) preceding its change; refuted items are dropped as named residuals.
- A wrapper session started after the change can spawn a subagent and perform an Edit without a permission prompt (observed in a live wrapper launch).
- A deliberately broken permission config makes `/next-plan` abort before the approval gate with the remedy text naming the misconfiguration.

## Notes

- **Invariant exposure: none of the runtime kind.** Agent scripts and skill text only — no determinism/CRC, `kiVersion`/`.pack`, replay, client/server guard, or allocation-tracked path is touched. The exposure is process-level: wrapper launch configuration can block other sessions if broken, so verify a fresh wrapper launch end-to-end before landing.
- Step 1's root-cause finding decides between the two step-2 mechanisms; if neither is honored by the host, that is the residual to present to the user — do not invent a third mechanism.
