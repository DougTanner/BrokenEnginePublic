# AgentHarness

Standalone Windows console application for the loopback client/server agent protocol and exclusive harness ownership. It owns no repository, build, landing, queue, or plan behavior.

- Use `Tools\AgentHarness\Platforms\VisualStudio2026\Output\AgentHarness.exe` only from the provisioned primary Output link. Source changes build through AgentTools candidate production and session-landing promotion (see `/compile`).
- Preserve the length-prefixed loopback JSON framing, request/response limits, `--owner` heartbeat behavior, and exit codes (`0` success, `2` negative JSON response, `1` usage/transport/OS failure).
- `lock token|claim|status|release|steal|heartbeat` operates only on harness `--key` values; do not add `--domain`, landing leases, build, or plan commands.
- Harness lock staleness model: the executable only records `heartbeatAt`/`heartbeatPid` on owned commands and never evaluates them; the calling agent judges staleness (heartbeat older than five minutes) and performs `steal`, per `.agents/skills/agent-harness/SKILL.md`.
- Keep source/header membership synchronized between `AgentHarness.vcxproj` and `.filters`. Shared Windows/coordination code lives in `Tools/ToolCommon` and is compiled by both tool projects.
