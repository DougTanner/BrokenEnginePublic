# /Projects/BrokenEngineSandbox/Source/Profile/

Game-specific profiling extending `engine::ProfileManagerBase` with game counters and timers.

**Global**: `game::gpProfileManager`

## Architecture

Game enums begin at `engine::kEngineCpuCounterCount` / `engine::kEngineCpuTimerCount` so indices compose into one contiguous space. Network overlay display and per-second rate counters live in the game-derived class (not the engine base) because they depend on game-side session state.

## Invariants

- **`kCpuTimerFrameUpdate` must remain the first game CPU timer** — the engine base's `FormatFpsHeader` reads this game enum by name.
- Indented display-name strings encode overlay hierarchy; preserve the leading-space convention when adding entries.
- Network metrics append `"!"` when network simulation is enabled and they exceed `engine::NetworkSimulationBounds`; suppressed under fast-forward (`miTimeMultiply > 1`).
- `NetworkGraphs.cpp` is fully `#if defined(BT_CLIENT)`-wrapped, includes `Pch.h` itself, and must appear only in the client vcxproj. Its `SmoothedGetter` reaches into `common::Smoothed` internals — the hard-coded capacity must track `common::Smoothed<T>`.

## See Also
- Engine profiling base: [../../../../Engine/Source/Profile/CLAUDE.md](../../../../Engine/Source/Profile/CLAUDE.md)
