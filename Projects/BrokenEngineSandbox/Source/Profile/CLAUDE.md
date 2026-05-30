# /Projects/BrokenEngineSandbox/Source/Profile/

Game-specific profiling extending `engine::ProfileManagerBase` with game CPU counters and timers (no game GPU timers/counters).

**Global**: `game::gpProfileManager`

## Architecture

Game enums begin at `engine::kEngineCpuCounterCount` / `engine::kEngineCpuTimerCount` so indices compose into one contiguous space; virtual dispatch routes by index. The CPU-timer hierarchy mirrors the frame phases (interpolate / post-render / render) and counters track per-collection populations and rendered subsets.

The client-only overlay's Frames and Network screens (and the ImPlot trend graphs beside them) live here rather than in the engine base because they read game session and grid state — active grid footprint, reconciliation tick rates, and smoothed clock/RTT/rollback series fed in each frame by the client session.

## Invariants

- **`kCpuTimerFrameUpdate` must remain the first game CPU timer** — the engine base's `FormatFpsHeader` reads this game enum by name.
- Indented display-name strings encode overlay hierarchy; preserve the leading-space convention when adding entries.
- Network metrics append `"!"` when network simulation is enabled and they exceed `engine::NetworkSimulationBounds`; suppressed under fast-forward (`miTimeMultiply > 1`).
- `NetworkGraphs.cpp` is fully `#if defined(BT_CLIENT)`-wrapped, includes `Pch.h` itself, and must appear only in the client vcxproj. Its `SmoothedGetter` reaches into `common::Smoothed` internals — the ring capacity derives from `common::Smoothed<int64_t>::kiCapacity` rather than a hard-coded literal.

## See Also
- Engine profiling base: [../../../../Engine/Source/Profile/CLAUDE.md](../../../../Engine/Source/Profile/CLAUDE.md)
