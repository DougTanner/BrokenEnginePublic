# /Projects/BrokenEngineSandbox/Source/Profile/

Game-specific profiling extending `engine::ProfileManagerBase` with game CPU counters and timers (no game GPU timers/counters).

Global: `game::gpProfileManager`

## Architecture

Game enums begin at `engine::kEngineCpuCounterCount` / `engine::kEngineCpuTimerCount` so indices compose into one contiguous space; base accessors route by index using the game arrays and name tables passed to its constructor. The CPU-timer hierarchy mirrors the frame phases (interpolate / post-render / render) and counters track per-collection populations and rendered subsets. The ProfileManager is constructed in `Main.cpp` before the engine managers; its constructor starts the total boot timer.

The client-only overlay's Frames and Network screens (and the ImPlot trend graphs beside them) live here rather than in the engine base because they read game session and grid state — active grid footprint, reconciliation tick rates, and smoothed clock/RTT/rollback series pushed in by the client session and reconciler. The Network screen writes its text into the FPS-header slot (the FPS header only renders on the CPU/GPU screens); Frames uses its own slot.

## Invariants

- The engine compiles against three game timer enums by name: `kCpuTimerFrameUpdate`, `kCpuTimerFrameInterpolate`, `kCpuTimerFramePostRender` (`GameBase.cpp` brackets the frame phases with them; the FPS header reads the first for total frame time). Renaming or removing any of them breaks the engine build.
- Display names live in `static_assert`-guarded name tables in `ProfileManager.h` (mirroring the engine convention; the struct rows carry only runtime state). Indented strings encode overlay hierarchy; preserve the leading-space convention when adding entries.
- When network simulation is enabled, Network-screen metrics append `"!"` when out of tolerance: RTT/loss compare against `engine::GetNetworkSimulationConfig`, the reconciliation rates against `engine::GetNetworkSimulationBounds`, rollback against a hard-coded cap. Fast-forward (`miTimeMultiply > 1`) bypasses only the loss comparison (header shows `BYPASS`); the other warning indicators stay live.
- Reconciliation counters submit every sample, including zero, so the one-second windows age old activity out instead of leaving stale nonzero rates on the Network screen.
- Server raw NavQuery measurements are latched only after active-cell workers join and only for accepted normal 1/1 ticks. Load/reset, replay, and other nonaccepted or no-dispatch updates clear pending raw values and unpublished arms while retaining any already-published event; the one-slot event payload stays immutable until its exact sequence is acknowledged, and overrun means the measurement was not captured safely.
- `NetworkGraphs.cpp` is fully `#if defined(BT_CLIENT)`-wrapped, includes `Pch.h` itself, and must appear only in the client vcxproj. Its `SmoothedGetter` reaches into `common::Smoothed` internals — the ring capacity derives from `common::Smoothed<int64_t>::kiCapacity` rather than a hard-coded literal.

## See Also
- Engine profiling base: `../../../../Engine/Source/Profile/AGENTS.md`
