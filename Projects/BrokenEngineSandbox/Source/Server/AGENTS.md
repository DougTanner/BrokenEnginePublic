# Server - Headless Monitoring Window

Server-only GDI monitoring for simulation statistics, profiling, memory, and the active-coordinate map. It has no Vulkan dependency.

## Ownership and Cadence

- Engine `Main.cpp` owns the window, message loop, and repaint scheduling. Game code owns statistic aggregation, paint content, and click handling because it reads concrete game collections and profile counters.
- Statistic aggregation runs per tick only when `kbProfiling`; when disabled, profile counters and the Profile tab remain empty while tick/time and the map stay live.
- Full-window painting is throttled and double-buffered to keep the server loop responsive; click-driven invalidation may repaint immediately.
- The map renders North (+y) upward and distinguishes client-authorized, active, idle, and subscribed coordinates.

## Profile Presentation

- Reuse the engine CPU timer/counter formatters and one `TickVisibilityCadence` decision per paint so timer and counter rows change visibility together.
- Build transient text in the thread-local workbuffer. UI rows hide sustained zeros, while the server agent profile query returns raw rows including zeros.

## See Also

- [Engine Profile](../../../../Engine/Source/Profile/AGENTS.md)
- [Game Profile](../Profile/AGENTS.md)
