<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-20T00:49:13.000Z","dependsOn":[]} -->
# Architecture: Harness Lock Integrity

## Context
Source: /external-architecture-review on `Tools/` recursively. AgentHarness accepts socket commands without a claimant token, can let a long valid command outlive its heartbeat, and makes lock decisions from parseable records without validating their coordination envelope.

## Design

### `Tools/AgentHarness/HarnessLockCommands.cpp`
- Add a harness-record validator based on `coordination::ValidateMetadataEnvelope` plus harness-specific schema/domain/key fields, and apply it after every `ReadMetadata` before `status`, owner comparison, heartbeat, release, or steal decisions at lines 156-235 and 238-259; retain corrupt-record recovery only as an explicit, separately authorized path. [~30m]

### `Tools/AgentHarness/AgentHarness.cpp`
- Require a valid current owner for every socket command instead of treating `--owner` as optional at lines 118-194, then keep the claim live during blocking connect/send/receive at lines 217-260 with periodic guarded heartbeat refresh bounded by the command deadline; abort if ownership is lost. [~1h]

## Critical files
- `Tools/AgentHarness/AgentHarness.cpp`
- `Tools/AgentHarness/HarnessLockCommands.h`
- `Tools/AgentHarness/HarnessLockCommands.cpp`
- `Tools/AgentHarness/AGENTS.md`
- `.agents/skills/agent-harness/SKILL.md`

## Out of scope
- Changing length-prefixed JSON framing, request/response size limits, endpoint commands, or game-side transport.
- Making AgentHarness itself judge five-minute staleness or perform automatic steals.
- Adding landing, build, queue, or repository behavior to AgentHarness.

## Acceptance criteria
- No socket command can execute without proving ownership of the default harness claim.
- A valid command lasting five to ten minutes keeps its claim fresh, and ownership loss stops the command.
- Parseable but schema/domain/key-invalid lock metadata cannot be status-reported as valid, heartbeated, released, or stolen through normal paths.
- AgentHarness lock and socket fixtures preserve exit codes `0`, `1`, and `2` and documented JSON framing.

## Notes
- Invariant exposure: exclusive harness command ordering and reliability of runtime/replay acceptance evidence; no direct simulation CRC, `.pack`, replay format, client/server layout, or allocation-tracked runtime exposure.
- Tier 3 trigger: cross-process ownership and long-running command coordination.
