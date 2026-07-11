# AgentCli - Harness Client and Workflow Coordinator

## Overview

Standalone Windows console application for the agent socket protocol and local development coordination. The canonical installed v2 executable lives at `%LOCALAPPDATA%\BrokenEngine\AgentCli\v2\AgentCli.exe`; use the `/compile` skill for bootstrap and build commands.

## Modes

- **Socket** sends length-prefixed JSON to a loopback agent port. Harness commands pass the current owner token so each command atomically refreshes that claim's heartbeat before connecting.
- **Lock** owns PC-global harness, plan, and landing claims beneath `%LOCALAPPDATA%\BrokenEngineLocks`. Logical keys are normalized and SHA-256 hashed; transitions serialize through an exclusive guard and update metadata atomically.
- **Build** serializes writers per target basename within the current worktree, launches MSBuild in a kill-on-close Job Object, and forwards its exit code. Selective builds query evaluated project items and `IntDir`, then invalidate only the requested `.cpp` objects.
- **Install** atomically copies the running Release executable to the canonical v2 path.

## Invariants

- Exit code `0` is success, `2` is a valid state conflict or negative socket response, and `1` is usage, transport, or OS failure.
- `harness` and `plan` locks use `--key`; `landing` locks use the canonicalized `--repo` path. Release and steal are conditional on the recorded owner token.
- Socket protocol framing and response limits must stay compatible with the client/server agent endpoints.
- Keep all new source and header files in both the AgentCli project and filters file.

## See Also

- [Root AGENTS.md](../../AGENTS.md) - Landing workflow and harness command families
- [`agent-harness` skill](../../.claude/skills/agent-harness/SKILL.md) - Ownership, launch, command, and shutdown workflow
- [`compile` skill](../../.claude/skills/compile/SKILL.md) - Bootstrap and supported build invocations
