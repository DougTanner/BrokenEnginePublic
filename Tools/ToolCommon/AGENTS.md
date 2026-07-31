# ToolCommon

Internal Windows and coordination support compiled directly into both tool executables. It provides shared resource, encoding, token, process-execution, and lock-storage primitives, but no command entry point or tool-specific lock policy.

- Keep this code independent of socket transport, repository workflows, and command dispatch; those contracts belong to AgentHarness and WorktreeCli.
- AgentTools is the collective name for the two tool executables, AgentHarness and WorktreeCli — a former single tool that was split.
- The AgentTools intentionally build without a precompiled header. Put shared standard-library and third-party consumption headers in `ToolCliCommon.h`; keep library implementation headers local to their implementation units when applicable.
- Keep `WorktreeCli.vcxproj` and `AgentHarness.vcxproj`, including their filters, synchronized for every shared source or header.
- Which tool the code is compiled into decides which coordination mechanism it uses, and that tool validates its arguments before calling the shared locator and storage helpers.
