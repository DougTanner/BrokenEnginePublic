# ToolCommon

Internal Windows and coordination support compiled directly into both tool executables. It provides shared resource, encoding, token, process-execution, and lock-storage primitives, but no command entry point or tool-specific lock policy.

- Keep this code independent of socket transport, repository workflows, and command dispatch; those contracts belong to AgentHarness and WorktreeCli.
- The AgentTools intentionally build without a precompiled header. Put shared standard-library and third-party consumption headers in `ToolCliCommon.h`; keep library implementation headers local to their implementation units when applicable.
- Keep `WorktreeCli.vcxproj` and `AgentHarness.vcxproj`, including their filters, synchronized for every shared source or header.
- The tool boundary selects the coordination domain and validates its arguments before calling the shared locator and storage helpers.
