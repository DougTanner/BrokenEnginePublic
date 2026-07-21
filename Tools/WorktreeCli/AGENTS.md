# WorktreeCli

Standalone Windows console application for repository coordination. It owns serialized MSBuild invocation, landing leases, machine-local plan queues, row claims, and queue validation/mutation. It never connects to a game endpoint or owns the AgentHarness lock.

## Executable and Commands

Use `Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe` through the provisioned primary Output link. Routine linked-worktree workflows do not build or modify that output; source changes use the `/compile` candidate/promotion path.

- `lock token|claim|status|refresh|recover|release|steal` operates on landing locks identified by `--repo`. Lease state, not claimant process provenance, determines liveness.
- `plan` owns queue initialization/validation, add/update/claim/complete operations, and owner/session claims. Queue lifecycle and authoring policy live in [`Documents/Plans/AGENTS.md`](../../Documents/Plans/AGENTS.md).
- `plan order add --request-sha256` optionally binds the transaction to the exact bounded request bytes read before JSON parsing. Finalization always supplies this approval-bound digest; generic callers may omit it.
- `build` prints one `broken-engine-build-result/v1` JSON object to stdout; human progress goes to stderr. It retains the combined MSBuild stream in the invoking worktree's ignored `Temp\AgentBuildLogs\` path and parses structured diagnostics from that same stream.
- Exit code `0` is success, `2` is a state conflict or negative result, and `1` is usage, transport, or OS failure.

`BROKEN_ENGINE_MSBUILD_PATH` pins discovery and fails when invalid. `BROKEN_ENGINE_BUILD_LOCK_WAIT_SECONDS` may only shorten the standard lock wait and exists for fixtures.

## Coordination State

Locks, claims, and scored queue rows live under `%LOCALAPPDATA%\BrokenEngineLocks`. Queue files are keyed by the Git common directory. `plan order init` is idempotent and refuses to overwrite non-empty state without explicit force.

Validation reports a missing plan as non-blocking only when a live claim owns it or primary contains it while the session has a stale wrapper baseline. A plan absent from both trees without a claim blocks validation.

Enumeration skips unreadable or schema-invalid claim records and reports the exact path; operations targeting that record fail. Recovery removes only the diagnosed `.lock` file. Never broadly delete coordination state.

## Project Ownership

Keep source membership synchronized between `WorktreeCli.vcxproj` and `.filters`. Shared Windows and coordination code belongs in `Tools/ToolCommon` and is compiled into both tools. Wrapper admission and maintenance use `.agents/scripts/WorktreeCliSessionExclusion.psm1`; bootstrap checks both WorktreeCli and AgentHarness outputs.
