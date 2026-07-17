# Plan Order Fixture Long-Path Failure

## Context

The canonical queue acceptance harness `.agents/scripts/Test-WorktreeCliPlanOrder.ps1` fails its `finalize landing holds landing then queues across primary advance and unwinds failures` case when the repository checkout sits under a deep path (observed in a wrapper session worktree at `C:\Users\dougt\.claude\worktrees\BrokenEnginePublic\<guid>`). The `Invoke-Git $fixture.Primary @('rebase', $verifiedCommit)` call (`.agents/scripts/Test-WorktreeCliPlanOrder.ps1:632`) exits 128 with `fatal: failed to stat '<sha>...<sha>': Filename too long`. The failure reproduces identically with the unmodified provisioned WorktreeCli at the session baseline, so it is environmental to the fixture layer, not a WorktreeCli defect: fixture repositories are created under `<worktree>\Temp\WorktreeCliPlanOrderFixtures\<guid>\<case>\primary`, and the combined depth exceeds Windows path limits inside `git rebase`. Ten of eleven cases pass in the same run; only this case invokes `git rebase`.

Acceptance gap: the harness is the decisive acceptance check for queue/claim changes (the poisoned-claim recovery change that queued this plan relied on it, and tool coordination changes generally require it to pass), but it cannot fully pass in a wrapper session worktree, where that acceptance evidence is produced.

Additional wrapper-worktree observation (2026-07-16): the `corrupt row claim is skipped with a diagnostic…` case also failed from a deep wrapper worktree — `claim-next` exited 2 with `queue-lock-acquire-failed` / `invalid-row-claim-snapshot` and stderr `plan row metadata is unreadable or invalid` where the case expects the corrupt claim to be skipped with a diagnostic. Design step 1's root-cause verification should determine whether this shares the long-path mechanism or is a distinct fixture-layer defect.

## Design

1. **Verify the root cause first.** Reproduce the failing case from a deep checkout and confirm the mechanism (path length beyond `MAX_PATH` inside `git rebase`, and whether `core.longpaths true` in the fixture repository config resolves it). If the mechanism differs, record the corrected cause before choosing a fix.
2. Apply the smallest fixture-layer fix so the whole suite passes from a wrapper session worktree — candidates, pending verification: set `core.longpaths true` in `New-FixtureRepository` alongside the existing `core.autocrlf`/`core.eol` config, or shorten the fixture root. Do not change WorktreeCli source or the case's assertions.

## Critical files

- `.agents/scripts/Test-WorktreeCliPlanOrder.ps1` — `New-FixtureRepository` (fixture git config), `$script:FixtureRoot`, and the `finalize landing …` case's `Invoke-Git … rebase` call

## Out of scope

- WorktreeCli C++ changes and lock/claim semantics.
- Reshaping the fixture suite (already done by the completed queue-process spine-diet plan; the culled harnesses no longer exist).

## Acceptance criteria

- The verified root cause is recorded before the fix.
- `Test-WorktreeCliPlanOrder.ps1` passes all cases against the provisioned WorktreeCli from a wrapper session worktree under `C:\Users\dougt\.claude\worktrees\...`.

## Notes

- Test-harness only: no game runtime, determinism/CRC, wire, `.pack`/`kiVersion`, or allocation-tracked exposure. No WorktreeCli rebuild required.
