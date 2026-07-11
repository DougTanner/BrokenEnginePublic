# Worktree-Based Concurrent Session Coordination

## Context

Multiple agent sessions working the repo at once repeatedly hit a wall: one session needs to
build-and-run (compile, run the agent-harness, verify) while another's uncommitted edits leave the
shared working tree non-compiling. Because the client/server executable is a single linked artifact,
scheduling sessions onto *disjoint file sets* (the previous approach) does not give them a disjoint
*build* — a disjoint edit elsewhere still breaks the verifier's compile.

The user has therefore **reversed the earlier decision** to reject worktree-per-session isolation:
git worktrees (independent, individually-compilable checkouts) are now the accepted concurrency model.
This plan supersedes `NextPlanConcurrencyCoordination.md` (removed this session), salvaging its still-good
mechanisms and re-framing them for the worktree world. The first coordination piece already landed: the
agent-harness advisory lock was relocated to a fixed **user-global** path
(`%LOCALAPPDATA%\BrokenEngineHarness\agent-harness.lock`) so it serializes harness sessions across
worktrees (`Tools/AgentCli/AgentCli.cpp` `TouchHarnessLock`, `.claude/skills/agent-harness/SKILL.md`).
That relocation is the template this plan generalizes.

## Design

Investigate and land as skill/process text (`.claude/skills/next-plan/SKILL.md`, `Documents/Plans/AGENTS.md`,
and a worktree-lifecycle section wherever it best lives). No engine/C++ change.

1. **Worktree lifecycle for a session.** Define create → build → verify → merge-back → cleanup: when a
   session spins a worktree, how it builds (note the cost — a fresh worktree's `Output/` is gitignored and
   empty, so it needs a full MSBuild/PCH build before anything runs), and how it removes the worktree when
   done. Decide the branch/naming convention and the `git worktree remove` discipline.
2. **Landing-zone file merge discipline (the core worktree problem).** Shared files exist in *every*
   worktree checkout and must reconcile to `main` without loss: `Order.md`, vcxproj/`.filters`, root and
   subsystem `AGENTS.md`, `.claude/skills/*/SKILL.md`. Define the merge/rebase workflow and conflict
   handling for these — the previous plan's "re-read-before-edit, gate on source-file overlap only" policy
   assumed one shared tree; under worktrees this becomes git merge/rebase sequencing. Enumerate the
   landing-zone list explicitly.
3. **PC-global claim coordination (generalize the harness-lock pattern).** `[CLAIMED]` in `Order.md` is a
   per-checkout copy under worktrees, so it no longer serializes claims across trees. Reuse the harness-lock
   template: a fixed user-global coordination point (a `%LOCALAPPDATA%` lock, or `main`'s `Order.md` as the
   single source of truth) so plan-claims are atomic across worktrees. Carry over the old plan's
   `[CLAIMED <date>]` timestamps with **warn-only** staleness (a paused session holds a claim legitimately —
   never auto-unclaim) and the atomic-`noclobber`/atomic-rename claim/steal mechanics already proven by
   `msbuild.sh` and the harness lock.
4. **Overlap detection, re-framed.** With worktrees isolating uncommitted edits, source-file overlap between
   concurrent sessions is no longer a shared-tree stomp — its role shifts to *sequencing landing-zone merges*
   and flagging plan-pairs likely to conflict on `main`. Decide whether the priority walk still skips
   overlapping candidates or just warns.

Manifest-accuracy caveat (carried over): overlap/merge reasoning is only as good as each plan's
`## Critical files`; keep that section complete (already a `Documents/Plans/AGENTS.md` authoring rule).

## Critical files

- `.claude/skills/next-plan/SKILL.md` — selection walk, claim coordination, worktree lifecycle, merge-back.
- `Documents/Plans/AGENTS.md` — `[CLAIMED <date>]` marker format, cross-worktree claim source-of-truth.
- (reference, already landed) `Tools/AgentCli/AgentCli.cpp` `TouchHarnessLock`, `.claude/skills/agent-harness/SKILL.md` — the user-global-lock template this plan generalizes.

## Out of scope

- The agent-harness lock relocation itself — already landed this session.
- Any engine/C++/CRC/wire/build change — skill/process text only.
- Concurrent *sims* on one PC (per-instance game/discovery ports + discovery pairing) — separate, large
  networking effort; unrelated to session coordination.
- Auto-unclaiming stale claims — remains user-only.

## Notes

- **Invariant exposure.** None — skill/docs/process text only; no code, CRC, wire, or build change.
- **Supersedes** `NextPlanConcurrencyCoordination.md` (its worktree rejection is reversed; removed this session).
- **Open decisions for grill:** claim source-of-truth across worktrees (user-global lock file vs `main` `Order.md`);
  worktree naming/branch + cleanup policy; empty-`Output/` first-build cost mitigation; exact landing-zone file
  list and its merge/conflict workflow; whether overlap detection skips vs warns.
