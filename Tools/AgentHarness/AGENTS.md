# AgentHarness

Standalone Windows console application for the loopback client/server agent protocol and exclusive harness ownership. It owns no repository, build, landing, queue, or plan behavior.

- Use `Tools\AgentHarness\Platforms\VisualStudio2026\Output\AgentHarness.exe` only from the provisioned primary Output link. Source changes require the explicit AgentTools primary-maintenance workflow.
- Preserve the length-prefixed loopback JSON framing, request/response limits, `--owner` heartbeat behavior, and exit codes (`0` success, `2` negative JSON response, `1` usage/transport/OS failure).
- `lock token|claim|status|release|steal` operates only on harness `--key` values; do not add `--domain`, landing leases, build, or plan commands.
- Keep source/header membership synchronized between `AgentHarness.vcxproj` and `.filters`. Shared Windows/coordination code lives in `Tools/ToolCommon` and is compiled by both tool projects.
