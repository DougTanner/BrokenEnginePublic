# /next-plan Concurrency Coordination — Overlap Detection, Wait Mode, Claim Staleness

## Context

The `[CLAIMED]` marker in `Documents/Plans/Order.md` is an advisory lock on the *plan*, not on its *file set*. Observed failure mode (2026-07-09 session): `/next-plan` selected `Agent/AgentClickScrollClippedWidgets.md` while a concurrent session held `Agent/AgentSetSliderCtrlTyping.md` — both edit `Engine/Source/Agent/AgentInput.cpp` — and the user had to notice the overlap and manually instruct the session to watch `Order.md` and wait. Claude Code has no cross-session lock/queue primitive; the shared-file advisory-lock approach is the right architecture, but the skill should detect file-set overlap and schedule around it automatically, so the user can kick off many `/next-plan` sessions at once and they work side-by-side without stepping on each other.

Facts established in that session, to build on:
- `Edit`'s modified-since-read rejection already serializes concurrent `Order.md` writers (observed live; the skill's re-read-and-retry rule handled it) — claims are effectively atomic; no new locking mechanism is needed.
- Claimed plan files survive on disk until completion, so any session can read a claimed plan's `## Critical files` to compute its file set.
- A claim can be *live but paused* for hours legitimately (e.g. `Agent/AgentPauseTimescaleEmptyServer.md`'s session sat waiting for user go-ahead) — timestamp-based staleness may only warn, never auto-unclaim.
- Alternatives considered and rejected as the primary mechanism (record, do not re-litigate): worktree isolation per session (shared landing-zone files — `Order.md`, vcxproj, AGENTS.md — plus heavy MSBuild/PCH builds and determinism-sensitive merges make it a poor fit); single-manager fan-out (real queueing but serializes all review pipelines through one context window, fights the many-sessions workflow); pre-Edit hook enforcement (enforcement, not scheduling — optional hardening at most, see Design item 5).

## Design

Investigate, refine, and land as `.claude/skills/next-plan/SKILL.md` text changes (with any spillover into `Documents/Plans/AGENTS.md`'s claim-marker documentation):

1. **Overlap detection in the priority walk (the queue).** For each candidate row, compute its file set — union of its `## Critical files` section, any `## File Groups` entry naming it, and any `## Dependencies` shared-file bullet — and intersect with the union of file sets of all `[CLAIMED]` plans (read from their on-disk plan files). Overlap → skip the candidate and continue walking, exactly like a `[CLAIMED]` row. Investigate: where in Step 1 this slots (before or after dependency resolution); whether the skip should be reported to the user ("skipped N overlapping candidates"); cost bound (claimed plans are few — full reads are fine).
2. **Wait mode for explicit targets.** When the user passes a plan as the argument and its file set overlaps a claimed plan: claim it anyway (reserving queue position), run Steps 3–7 + the grill + approval (all read-only vs the contested files), then arm a background poll of `Order.md` (grep the overlapping plan paths for `[CLAIMED]`, ~30 s interval) and begin implementation only when the overlap clears. Investigate: whether waiting should also re-refresh line citations against the other session's landed edits before implementing (recommended — the 2026-07-09 session planned exactly that); interaction with skill Step 2's existing "row already CLAIMED → ask about reclaim" branch.
3. **Claim timestamps + staleness reporting.** Marker becomes `[CLAIMED 2026-07-09]` (date suffices; time optional). A session that would wait on (or skip past) a claim older than a threshold (propose: 24 h) surfaces it to the user as possibly stale instead of silently queuing behind it — but never auto-unclaims (the paused-session case above is legitimate). Backwards compatibility: bare `[CLAIMED]` (no timestamp) must keep parsing as claimed during rollout. Update `Documents/Plans/AGENTS.md`'s marker description in the same session.
4. **Landing-zone file policy.** Reviews/doc steps touch shared files beyond `## Critical files` (`Order.md`, root/subsystem `AGENTS.md`s, `.claude/skills/*/SKILL.md`, vcxproj/filters). Gating on these would make every plan-pair conflict. Policy to encode: gate scheduling on *source-file* overlap only; treat landing-zone files as always-shared-but-mergeable — writers re-read immediately before editing and retry on modified-since-read, exactly the existing `Order.md` contract. Enumerate the landing-zone list explicitly in the skill text.
5. **Lock-file mutex around the claim critical section.** The Edit modified-since-read + retry contract serializes writers but leaves a work-wasting window: two sessions can both read the table, both select, and one loses the race after doing selection work. Investigate a true mutex around each `Order.md` read-modify-write section (claim, unclaim, row insert/remove): acquire `Temp/next-plan.lock` via shell atomic create — `set -o noclobber; echo "<session-id> <timestamp>" > Temp/next-plan.lock` (noclobber makes the redirect fail if the file exists; works in Git Bash on Windows; `mkdir`-based locking is the classic fallback) — do the read-select-edit, delete the lock. Holder id + timestamp inside the lock enable stale-lock breaking (lock sections are seconds long, so break after ~1 min is safe — unlike plan claims, which are hours-long and never broken automatically). Decide lock location (repo `Temp/` gitignored vs `%TEMP%` — must be one path all sessions of this repo resolve identically, so a repo-relative gitignored path is likely right) and whether the wait-mode poll also takes it (no — polling is read-only; only mutations lock). If adopted, the Edit-retry rule stays as backstop for skill versions/sessions that don't take the lock.
6. **(Optional, investigate only)** Hook-based enforcement: a PreToolUse Edit/Write hook that warns (not blocks) when the target file belongs to another session's claimed plan file set — belt-and-suspenders under bypass-permissions. Deliverable is a feasibility note, not an implementation; if worthwhile it becomes its own follow-up plan.

Manifest-accuracy caveat to state in the skill text: overlap detection is only as good as `## Critical files`; plan authors must keep that section complete (already a `Documents/Plans/AGENTS.md` authoring rule — cross-reference it).

## Critical files

- `.claude/skills/next-plan/SKILL.md` — Steps 1/2 (selection walk + claim), Preconditions (concurrency contract), Edge cases (stale claim, wait mode).
- `Documents/Plans/AGENTS.md` — `[CLAIMED]` marker format (timestamp), authoring rule cross-reference for `## Critical files` completeness.

## Out of scope

- Any C++/engine change — this is skill/process text only.
- Worktree-per-session or single-manager orchestration modes (rejected above; do not redesign).
- Implementing the enforcement hook (item 5 is feasibility-note only).
- Auto-unclaiming stale claims — remains user-only.
- `Documents/Features/Order.md` — mirror the final wording there only if trivially identical; otherwise file a sibling follow-up.

## Acceptance criteria

- Two simulated sessions: session A claims a plan touching file X; session B's `/next-plan` walk skips every candidate whose file set contains X and selects the best disjoint plan instead, reporting the skip.
- Session B invoked with an explicit overlapping argument claims it, completes grill+approval, and demonstrably waits (documented poll loop) until A's claim clears before implementation.
- A `[CLAIMED <date>]` row older than the threshold produces a stale-claim warning naming the row, and bare `[CLAIMED]` still parses as claimed.

## Notes

- **Invariant exposure.** None — skill/docs text only; no code, CRC, wire, or build change.
- **Open decisions for grill:** staleness threshold value (24 h proposed); whether overlap-skip is silent or reported (reported proposed); whether wait mode re-runs citation refresh after the blocking session lands (yes proposed); exact landing-zone file list; adopt the `Temp/next-plan.lock` noclobber mutex (Design item 5) vs keep Edit-retry only, and its lock-file location.
