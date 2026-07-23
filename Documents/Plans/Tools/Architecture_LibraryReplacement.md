<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-20T00:49:13.000Z","dependsOn":[]} -->
# Architecture: Library Replacement

## Context
Source: /external-architecture-review on `Tools/` recursively. Both tools repeat manual command-line parsing and usage generation across increasingly deep verb trees; a focused CLI library could remove this mechanical surface while retaining tool-owned domain validation.

## Design

### AgentHarness and WorktreeCli command parsing
- Evaluate and, after explicit ThirdParty approval, replace the manual usage/argument/dispatch ranges in `AgentHarness.cpp:27-33,118-184,289-311`, `HarnessLockCommands.cpp:40-84,96-143`, `WorktreeCli.cpp:15-65`, `LandingLockCommands.cpp:50-105,109-153`, `PlanScheduler.cpp:142-220,1845-1883`, and `BuildCommand.cpp:656-683` with CLI11. The measured candidate spans total 3,578 bt-token-v1; retain semantic validation and dispatch, targeting approximately 2,500-3,000 bt-token-v1 net removal. CLI11 is header-only under BSD-3-Clause, which is on the repository allow-list, and is not currently present under `ThirdParty/`. [~1h]
- Preserve exact exit codes, stdout/stderr placement, BuildCommand's single-JSON stdout guarantee, `build --files ... --` passthrough, raw JSON positional input, and verb-dependent ownership/lease validation; wrap CLI11 so its default parse exceptions and help output cannot bypass these contracts. [~1h]

## Critical files
- `Tools/AgentHarness/AgentHarness.cpp`
- `Tools/AgentHarness/HarnessLockCommands.cpp`
- `Tools/WorktreeCli/WorktreeCli.cpp`
- `Tools/WorktreeCli/LandingLockCommands.cpp`
- `Tools/WorktreeCli/PlanScheduler.cpp`
- `Tools/WorktreeCli/BuildCommand.cpp`
- `Tools/ToolCommon/ToolCliCommon.h`
- `ThirdParty/`
- AgentTools Visual Studio project files

## Out of scope
- Moving domain validation into parser callbacks.
- Replacing process execution, Git plumbing, JSON, coordination storage, Plan metadata scheduling, MSBuild diagnostics, or harness framing.
- Accepting any CLI behavior change for convenience.

## Acceptance criteria
- User approval explicitly authorizes the new ThirdParty dependency before import.
- Imported source license is verified as BSD-3-Clause and upstream-pristine; CLI11 remains the sole parsing implementation.
- Both executables preserve accepted command lines, output channels/schemas, and exit codes across success, usage failure, and state-conflict fixtures.
- Net first-party removal is measured after integration and remains materially above the 500 bt-token-v1 threshold.

## Notes
- Invariant exposure: AgentHarness/WorktreeCli command-line protocol and build/Plan-claim operational contracts; no engine determinism/CRC, `.pack`, replay, client/server, or allocation-tracked runtime exposure.
- Risks: parser defaults can change output/exit behavior; broad header consumption can increase PCH-less build cost; passthrough and raw JSON arguments need explicit configuration.
- Tier 3 trigger: new ThirdParty dependency, shared tool CLI contract, both tool projects, and project membership. `/external-grill-plan` must confirm the dependency choice and exact compatibility surface after explicit user approval.
