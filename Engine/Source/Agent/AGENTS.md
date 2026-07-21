# Agent - Harness Transport and Client Automation

## Overview

Engine-owned agent infrastructure provides a loopback TCP JSON channel, main-thread command execution, deferred replies, synthetic client input, and UI snapshots. Game command semantics live in the project [Agent](../../../Projects/BrokenEngineSandbox/Source/Agent/AGENTS.md).

## Architecture

- `AgentCommandServer` is dormant unless the agent build flag and `--agent-port` enable it. A background `jthread` owns socket I/O; `Drain()` executes at defined client/server main-thread points and dispatches to `game::ExecuteAgentCommand`.
- Only one request is in flight. Deferred handlers return a poll function; later drains publish its first result under the original request ID. Connection generations discard replies from disconnected clients, and the drain-count timeout prevents a lost async result from blocking the channel indefinitely.
- Command handlers throw for invalid external input. `Drain()` converts exceptions to the protocol failure envelope.
- `AgentInput` is client-only and runs one frame-stepped synthetic script at a time. It feeds ImGui events and overlays the game `RawInput` snapshot. Physical input suppression is owned by the startup/input path.
- `AgentUiRegistry` is client-only and double-buffers fixed-capacity ImGui window/item snapshots. Publish after `ImGui::Render()`; label resolution reads only the completed buffer.
- Synthetic ImGui mouse position is reissued after the Win32 backend so physical cursor polling cannot overwrite it. It remains pinned for post-script UI inspection until the next script begins.

## Constraints

- Keep steady-state input and registry paths allocation-free.
- Client implementations remain whole-file `BT_CLIENT`; server hook stubs keep shared ImGui integration linkable.
- Do not execute game mutations or Vulkan/UI work on the socket thread.

## See Also

- [Engine Input](../Input/AGENTS.md)
- [Agent Harness](../../../Tools/AgentHarness/AGENTS.md)
