# AgentHarness

Standalone Windows console application for the loopback client/server agent protocol and exclusive harness ownership. It owns no repository, build, landing, queue, or plan behavior.

- Use `Tools\AgentHarness\Platforms\VisualStudio2026\Output\AgentHarness.exe` only from the provisioned primary Output link. Source changes build through AgentTools candidate production and session-landing promotion (see `/compile`).
- Preserve the length-prefixed loopback JSON framing, request/response limits, the connect phase retried until the `--timeout-ms` deadline (not a single attempt), and exit codes (`0` success, `2` negative JSON response, `1` usage/transport/OS failure). Socket commands require `--owner TOKEN`: after request acquisition and before Winsock/connect, AgentHarness proves ownership, refreshes it on the command thread whenever due during transport (60-second interval), and refreshes once more before response output. Ownership loss exits `1` and stops local response handling; it cannot retract game work already dispatched.
- Keep socket readiness and I/O on the command thread with nonblocking `select`; each connect, send, and receive phase bounds no-progress waiting by its absolute deadline.
- `lock token|claim|status|release|steal|heartbeat` operates only on harness `--key` values; do not add `--domain`, landing leases, build, or plan commands.
- Validate the metadata wrapper around the lock record before normal use or changes. The executable only records `heartbeatAt`/`heartbeatPid` on owned commands and never evaluates them; the calling agent judges staleness (heartbeat older than five minutes) and performs `steal`, per `.agents/skills/agent-harness/SKILL.md`.
- Keep source/header membership synchronized between `AgentHarness.vcxproj` and `.filters`. Shared Windows/coordination code lives in `Tools/ToolCommon` and is compiled by both tool projects.
