<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-06T21:50:41.173Z","dependsOn":[]} -->
# Cleanup: remove unreachable empty-owner early-out in RefreshHarnessHeartbeat

## Context

`RefreshHarnessHeartbeat` (`Tools/AgentHarness/HarnessLockCommands.cpp:244-266`) begins with:

```cpp
if (rOwner.empty())
{
    return true;
}
```

The branch is unreachable. Repository-wide, the only callers are the `RunSocketCommand` paths in `Tools/AgentHarness/AgentHarness.cpp` (`:639` and `:658` directly, `:149` via `RefreshHeartbeatIfDue` with the same owner), and every path first passes `ParseSocketCommandArguments`, which fails on an empty `--owner` (`AgentHarness.cpp:448-453`). The root `AGENTS.md` directive "Error handling at trust boundaries only" forbids defensive validation between our own functions; worse, if the invariant were ever broken this branch would report heartbeat success without stamping ownership, masking the bug instead of surfacing it.

Found and verified (caller enumeration confirmed) by `/external-deep-analysis` over `Tools/AgentHarness` (Directory scope). Pre-existing debt outside `Documents/Plans/Agents/AgentToolsDeepAnalysis.md`'s decomposition/duplication boundary.

## Design

Delete the four lines at `Tools/AgentHarness/HarnessLockCommands.cpp:246-249`. No signature, caller, or diagnostic change.

## Critical files

- `Tools/AgentHarness/HarnessLockCommands.cpp` — `RefreshHarnessHeartbeat`.

## In scope

- The empty-owner early-out at the top of `RefreshHarnessHeartbeat` in `Tools/AgentHarness/HarnessLockCommands.cpp`.

## Out of scope

- Every other statement of `RefreshHarnessHeartbeat`; all lock verbs; all transport code; envelope bytes, exit codes, diagnostic text.

## Risk tier and invariants

Change Workflow Tier 1 — local behavior-preserving removal of a proven-unreachable branch; no public signature or invariant exposure. Invariant: all reachable behavior byte-identical.

## Acceptance criteria

The diff is decisive (four-line deletion). AgentHarness and WorktreeCli both build clean through `/compile`.

## Notes

`Documents/Plans/Agents/AgentHarnessHeartbeatGuardDeadline.md` edits the same function independently; either order lands cleanly.
