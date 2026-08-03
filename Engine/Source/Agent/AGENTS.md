# Agent - Harness Transport and Client Automation

## Overview

Engine-owned agent infrastructure provides a loopback TCP JSON channel, main-thread command execution, deferred replies, synthetic client input, and UI snapshots, plus the build-agnostic shared command handlers. Game-specific command semantics live in the project Agent (`../../../Projects/BrokenEngineSandbox/Source/Agent/AGENTS.md`).

## Architecture

- `AgentCommandServer` is dormant unless the agent build flag and `--agent-port` enable it. A background `jthread` owns socket I/O; `Drain()` executes at defined client/server main-thread points and dispatches to `game::ExecuteAgentCommand`.
- `ExecuteSharedAgentCommand` owns the build-agnostic command handlers (process lifecycle, log inspection and control) that every game project shares; the game dispatcher tries it before any side-specific handler. It stays engine-generic — game state it reports, such as the current tick (-1 before game creation), is passed in as a plain `int64_t` rather than read from game globals.
- Construction binds on the startup thread with `SO_REUSEADDR` and retries a `WSAEADDRINUSE` bind (port still in TIME_WAIT after a rapid relaunch) for up to ~2.5 s before throwing `StartupException`. The same setting also lets a duplicate launch on the same port bind silently instead of failing fast; the harness's quit-and-wait-for-exact-PID relaunch rule is the guard against that misuse, not a bind-time check.
- Keep listener accept nonblocking so shutdown can serialize socket invalidation with accept under the transport lock. Keep active-connection readiness, receive, and send outside the transport lock; only nonblocking accept and socket ownership transitions remain serialized. Teardown requests stop before closing the listener and wakes the condition variable; the accepted socket remains nonblocking, and receive waits check stop before and after each 50 ms readiness wait. The listener flushes a published response despite stop under a single 3 s deadline, then performs the active connection's final close after I/O exits.
- Only one request is in flight. Deferred handlers return a poll function; later drains publish its first result under the original request ID. Connection generations (the reuse counter) discard replies from disconnected clients, and the drain-count timeout prevents a lost async result from blocking the channel indefinitely.
- Command handlers throw for invalid external input. `Drain()` converts exceptions to the protocol failure envelope.
- `AgentInput` is client-only and runs one frame-stepped synthetic script at a time. It feeds ImGui events and overlays the game `RawInput` snapshot. Physical input suppression is owned by the startup/input path.
- `AgentUiRegistry` is client-only and double-buffers fixed-capacity ImGui window/item snapshots. Publish after `ImGui::Render()`; label resolution reads only the completed buffer.
- Synthetic ImGui mouse position is reissued after the Win32 backend so physical cursor polling cannot overwrite it. It remains pinned for post-script UI inspection until the next script begins.

## Constraints

- Keep steady-state input and registry paths allocation-free.
- Client implementations remain whole-file `BT_CLIENT`; server hook stubs keep shared ImGui integration linkable.
- Do not execute game mutations or Vulkan/UI work on the socket thread.

## See Also

- Engine Input (`../Input/AGENTS.md`)
- Agent Harness (`../../../Tools/AgentHarness/AGENTS.md`)
