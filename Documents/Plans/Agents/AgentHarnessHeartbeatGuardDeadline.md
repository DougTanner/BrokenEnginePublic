<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-06T21:50:39.908Z","dependsOn":[]} -->
# Fix: AgentHarness transport heartbeat guard acquisition ignores the phase deadline

## Context

`Tools/AgentHarness/AGENTS.md` requires each connect, send, and receive phase to bound no-progress waiting by its absolute deadline, with ownership refreshed on the command thread whenever due during transport. The refresh path breaks that bound:

- During transport, `WaitForSocketReadiness`, `SendAll`, and `ReceiveAll` call `RefreshHeartbeatIfDue` (`Tools/AgentHarness/AgentHarness.cpp:143-155`), which calls `RefreshHarnessHeartbeat`.
- `RefreshHarnessHeartbeat` (`Tools/AgentHarness/HarnessLockCommands.cpp:244-266`) synchronously constructs `coordination::Guard` at line 255 with no knowledge of the active phase deadline.
- `Guard`'s constructor (`Tools/ToolCommon/CoordinationStore.cpp:25-45`) retries `CreateFileW` every 25 ms for up to `kuiGuardWaitMilliseconds` = 10'000 ms (`CoordinationStore.cpp:22`).

Any concurrent `lock claim|status|release|steal|heartbeat` command on the harness `default` key holds the same guard (`HarnessLockCommands.cpp:150`), so contention during a send or receive phase can extend no-progress waiting roughly ten seconds past the phase's absolute deadline. Verified by the deep-analysis Phase-3 reviewer as a reachable contract violation, not speculative contention.

Found by `/external-deep-analysis` over `Tools/AgentHarness` (Directory scope). Pre-existing debt; it requires a shared `Tools/ToolCommon` change, which `Documents/Plans/Agents/AgentToolsDeepAnalysis.md` explicitly routes out of its own boundary.

## Design

- `coordination::Guard` (`Tools/ToolCommon/CoordinationStore.h/.cpp`) gains an optional maximum-wait-milliseconds constructor parameter defaulting to the current `kuiGuardWaitMilliseconds`, so every existing call site keeps today's 10-second behavior without edits.
- `RefreshHarnessHeartbeat` gains a wait-budget parameter and passes it to `Guard`.
- `RefreshHeartbeatIfDue` gains the active phase's absolute deadline (already available as `rOperationDeadline` in `WaitForSocketReadiness` and `CheckDeadlineAndRefreshHeartbeatAfterNoProgress`, and as `deadline` in `ExchangeFramedSocketCommand`'s connect loop) and converts remaining time to the budget it forwards. The two pre/post-transport refresh calls in `RunSocketCommand` (`AgentHarness.cpp:639`, `:658`) have no phase deadline and keep the default budget.
- A guard not acquired within the budget is the existing acquisition-failure path: `RefreshHarnessHeartbeat` returns false and transport reports ownership loss exactly as today (`Fail("harness heartbeat refresh failed")`, exit 1).

## Critical files

- `Tools/ToolCommon/CoordinationStore.h` / `Tools/ToolCommon/CoordinationStore.cpp` — `Guard` constructor wait bound.
- `Tools/AgentHarness/HarnessLockCommands.cpp` / `HarnessLockCommands.h` — `RefreshHarnessHeartbeat` budget parameter.
- `Tools/AgentHarness/AgentHarness.cpp` — `RefreshHeartbeatIfDue` and its transport call sites.

## In scope

- `coordination::Guard` constructor signature (optional wait parameter with the current default) and its wait loop bound in `Tools/ToolCommon/CoordinationStore.h/.cpp`.
- `RefreshHarnessHeartbeat` signature and guard construction in `Tools/AgentHarness/HarnessLockCommands.cpp/.h`.
- `RefreshHeartbeatIfDue` and the deadline plumbing at its call sites in `WaitForSocketReadiness`, `CheckDeadlineAndRefreshHeartbeatAfterNoProgress`, `SendAll`, `ReceiveAll`, and `ExchangeFramedSocketCommand` in `Tools/AgentHarness/AgentHarness.cpp`.

## Out of scope

- Any behavior change at existing `Guard` call sites in `RunHarnessLockCommand` or in any `Tools/WorktreeCli` translation unit — all keep the 10-second default.
- Landing lock lease duration, acquisition ordering, compare-and-swap, or claim semantics.
- Envelope bytes, exit codes, diagnostic text, heartbeat interval, and lock verb surface.

## Risk tier and invariants

Change Workflow Tier 3. Trigger: the change touches `Tools/ToolCommon` coordination primitives compiled into both tools, a surface WorktreeCli's landing coordination depends on — cross-subsystem integration excluded from Tier 2. Invariants: WorktreeCli behavior byte-identical (default wait preserved at every existing call site, compile-checked); ownership-loss handling and exit codes unchanged; heartbeat stamping fields (`heartbeatAt`/`heartbeatPid`) unchanged.

## Acceptance criteria

- With the harness `default` guard held by a concurrent process during a transport phase, the heartbeat refresh returns within the phase's remaining budget and transport reports the existing ownership-loss failure at exit 1, instead of stalling up to 10 seconds past the deadline.
- Ordinary `lock` commands under contention still wait up to 10 seconds as today.
- AgentHarness and WorktreeCli both build clean through `/compile`.

## Notes

`Documents/Plans/Agents/AgentHarnessHeartbeatOwnerValidation.md` deletes an unreachable early-out in the same `RefreshHarnessHeartbeat` function; the plans are independent and either may land first — the later one rebases over a trivial context shift.
