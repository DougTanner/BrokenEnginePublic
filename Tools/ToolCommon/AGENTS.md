# ToolCommon

Internal Windows and coordination support compiled directly into both tool executables. It provides shared resource, encoding, token, and lock-storage primitives, but no command entry point or tool-specific lock policy.

- Keep this code independent of socket transport, repository workflows, and command dispatch; those contracts belong to AgentHarness and WorktreeCli.
- Keep `WorktreeCli.vcxproj` and `AgentHarness.vcxproj`, including their filters, synchronized for every shared source or header.
- The tool boundary selects the coordination domain and validates its arguments before calling the shared locator and storage helpers.
