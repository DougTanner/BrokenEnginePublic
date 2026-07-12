# AgentCli - Harness Client and Workflow Coordinator

## Overview

Standalone Windows console application for the agent socket protocol and local development coordination. The canonical installed v2 executable lives at `%LOCALAPPDATA%\BrokenEngine\AgentCli\v2\AgentCli.exe`; use the `/compile` skill for bootstrap and build commands.

## Modes

- **Socket** sends length-prefixed JSON to a loopback agent port. Harness commands pass the current owner token so each command atomically refreshes that claim's heartbeat before connecting.
- **Lock** owns PC-global harness, plan, and landing claims beneath `%LOCALAPPDATA%\BrokenEngineLocks`. Logical keys are normalized and SHA-256 hashed; transitions serialize through an exclusive guard and update metadata atomically. Landing claims use schema-3 owner leases (`claim --lease-seconds`, `status`, owner-only `refresh`, guarded expired `recover`, and owner-only `release`).
- **Build** serializes writers per target basename within the current worktree, launches MSBuild in a kill-on-close Job Object, and forwards its exit code. Selective builds query evaluated project items and `IntDir`, then invalidate only the requested `.cpp` objects.
- **Install** atomically copies the running Release executable to the canonical v2 path.

## Invariants

- Exit code `0` is success, `2` is a valid state conflict or negative socket response, and `1` is usage, transport, or OS failure.
- `harness` and `plan` locks use `--key`; `landing` locks use the canonicalized `--repo` path. Release is conditional on the recorded owner token; steal is owner-matched for harness, plan, and legacy landing records only.
- Landing lease duration is 60–86,400 seconds. Status is `live`, `expired`, or fail-closed `unverifiable`; malformed and legacy schema 1–2 records remain held/unverifiable. Exact owners may release legacy records, and explicitly approved owner-matched legacy `steal --lease-seconds` upgrades to schema 3. Schema-3 records never permit `steal`.
- Expired schema-3 recovery requires the expected owner and atomically replaces the record only after every registered non-bare, non-prunable worktree is inspectable and clear of `MERGE_HEAD`, `rebase-merge`, `rebase-apply`, `CHERRY_PICK_HEAD`, `REVERT_HEAD`, `BISECT_LOG`, and `sequencer`. This is a fail-closed snapshot, not exclusion against Git operations starting afterward.
- Socket protocol framing and response limits must stay compatible with the client/server agent endpoints.
- Keep all new source and header files in both the AgentCli project and filters file.

## See Also

- [Root AGENTS.md](../../AGENTS.md) - Landing workflow and harness command families
- [`agent-harness` skill](../../.claude/skills/agent-harness/SKILL.md) - Ownership, launch, command, and shutdown workflow
- [`compile` skill](../../.claude/skills/compile/SKILL.md) - Bootstrap and supported build invocations
