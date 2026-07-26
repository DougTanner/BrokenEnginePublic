# WorktreeCli

Standalone Windows console application for repository coordination. It owns serialized MSBuild invocation, landing leases, and the Git-backed `Documents/Plans` scheduler. It never connects to a game endpoint or owns the AgentHarness lock.

## Executable and Commands

Use `Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe` through the provisioned primary Output link. Routine linked-worktree workflows do not build or modify that output; wrapper session start incremental-rebuilds it in the primary so it tracks primary HEAD, and source changes use the `/compile` candidate/promotion path.

- `lock token|claim|status|refresh|recover|release|steal` operates on landing locks identified by `--repo`. Lease state, not claimant process provenance, determines liveness.
- `plan validate|claim-next|claim-status|unclaim|prepare-completion|prepare-rejection|release-after-landing|reparent-claims` is the only scheduler surface. It reads tracked `Documents/Plans` metadata, uses deterministic `(createdUtc,path)` selection, and stores only short-lived machine-local claim records. `claim-next` selects from the session worktree tree and requires session `HEAD` to be an ancestor of (or equal to) the primary tip, so only paths still present at the primary tip are newly claimable. `reparent-claims` is wrapper-invoked after a `git rebase --onto` re-parent and takes only `--new-baseline`: it mutates the live claims bound to the given worktree and branch whose recorded primary commit is no longer an ancestor of the worktree HEAD (exactly the claims healing would otherwise delete), plus the Temp receipts cryptographically chained to them; callers run no other scheduler operation mid-conflict until the rebase completes.
- `build` prints one `broken-engine-build-result/v1` JSON object to stdout; human progress goes to stderr. It retains the combined MSBuild stream in the invoking worktree's ignored `Temp\AgentBuildLogs\` path and parses structured diagnostics from that same stream.
- Exit code `0` is success, `2` is a state conflict or negative result, and `1` is usage, transport, or OS failure.

`BROKEN_ENGINE_MSBUILD_PATH` pins discovery and fails when invalid. `BROKEN_ENGINE_BUILD_LOCK_WAIT_SECONDS` may only shorten the standard lock wait and exists for fixtures.

## Coordination State

Landing locks and scheduler claims live under `%LOCALAPPDATA%\BrokenEngineLocks`. Scheduler claims are keyed by Git common directory and expire after 48 hours; invalid, expired, and orphaned local records self-heal. `Documents/Features` and `Documents/Investigations` are not scheduled.

Missing dependency paths are satisfied with a stale-edge notice. Invalid metadata and dependency cycles quarantine only their affected Plans component; unrelated valid plans remain claimable.

Executable markers contain exactly `createdUtc` and `dependsOn`; timestamps use canonical millisecond UTC (`yyyy-MM-ddTHH:mm:ss.fffZ`). Every tracked `Documents/Plans` document carries one at byte zero; an absent marker — including one displaced by a BOM — reports `invalid-metadata` naming the path. `AGENTS.md` and `CLAUDE.md` are exempt at any depth. Claim healing requires the recorded worktree, branch, common directory, and primary-baseline ancestry to remain valid at the worktree's current `HEAD`.

Enumeration skips unreadable or schema-invalid claim records and reports the exact path; operations targeting that record fail. Recovery removes only the diagnosed `.lock` file. Terminal preparation cleans only hidden regular atomic siblings matching the exact Plan-owned temporary filename shape, and classifies each manifest child by whether its on-disk marker still lists the terminal target rather than by whole-file digest, so a reconciled child body never conflicts; the terminal target itself stays digest-gated. Receipt-bound release proves the landed commit contains the claim baseline, is incorporated into the actual primary tip, and has terminal state in that tip before deleting the claim. Never broadly delete coordination state.

## Project Ownership

Keep source membership synchronized between `WorktreeCli.vcxproj` and `.filters`. Shared Windows and coordination code belongs in `Tools/ToolCommon` and is compiled into both tools. Transient operation claims around shared-tool consumption use `.agents/scripts/WorktreeCliSessionExclusion.psm1`; bootstrap incremental-rebuilds the WorktreeCli, AgentHarness, and ThirdParty primary outputs at every session start, and best-effort prebuilds DataPacker Release so new worktrees seed it by verified copy.
