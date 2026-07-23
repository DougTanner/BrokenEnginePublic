# WorktreeCli

Standalone Windows console application for repository coordination. It owns serialized MSBuild invocation, landing leases, and the Git-backed `Documents/Plans` scheduler. It never connects to a game endpoint or owns the AgentHarness lock.

## Executable and Commands

Use `Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe` through the provisioned primary Output link. Routine linked-worktree workflows do not build or modify that output; source changes use the `/compile` candidate/promotion path.

- `lock token|claim|status|refresh|recover|release|steal` operates on landing locks identified by `--repo`. Lease state, not claimant process provenance, determines liveness.
- `plan validate|claim-next|claim-status|unclaim|prepare-completion|prepare-rejection|release-after-landing` is the only scheduler surface. It reads tracked `Documents/Plans` metadata, uses deterministic `(createdUtc,path)` selection, and stores only short-lived machine-local claim records.
- `build` prints one `broken-engine-build-result/v1` JSON object to stdout; human progress goes to stderr. It retains the combined MSBuild stream in the invoking worktree's ignored `Temp\AgentBuildLogs\` path and parses structured diagnostics from that same stream.
- Exit code `0` is success, `2` is a state conflict or negative result, and `1` is usage, transport, or OS failure.

`BROKEN_ENGINE_MSBUILD_PATH` pins discovery and fails when invalid. `BROKEN_ENGINE_BUILD_LOCK_WAIT_SECONDS` may only shorten the standard lock wait and exists for fixtures.

## Coordination State

Landing locks and scheduler claims live under `%LOCALAPPDATA%\BrokenEngineLocks`. Scheduler claims are keyed by Git common directory and expire after 48 hours; invalid, expired, and orphaned local records self-heal. `Documents/Features` is not scheduled.

Missing dependency paths are satisfied with a stale-edge notice. Invalid metadata and dependency cycles quarantine only their affected Plans component; unrelated valid plans remain claimable.

Executable markers contain exactly `createdUtc` and `dependsOn`; timestamps use canonical millisecond UTC (`yyyy-MM-ddTHH:mm:ss.fffZ`). Claim healing requires the recorded worktree, branch, common directory, and primary-baseline ancestry to remain valid at the worktree's current `HEAD`.

Enumeration skips unreadable or schema-invalid claim records and reports the exact path; operations targeting that record fail. Recovery removes only the diagnosed `.lock` file. Terminal preparation cleans only hidden regular atomic siblings matching the exact Plan-owned temporary filename shape. Receipt-bound release proves the landed commit contains the claim baseline, is incorporated into the actual primary tip, and has terminal state in that tip before deleting the claim. Never broadly delete coordination state.

## Project Ownership

Keep source membership synchronized between `WorktreeCli.vcxproj` and `.filters`. Shared Windows and coordination code belongs in `Tools/ToolCommon` and is compiled into both tools. Wrapper admission and maintenance use `.agents/scripts/WorktreeCliSessionExclusion.psm1`; bootstrap checks both WorktreeCli and AgentHarness outputs.
