# /Projects/BrokenEngineSandbox/Source/Profile/

Game-specific profiling that extends `engine::ProfileManagerBase` with game counters and timers.

**Global**: `game::gpProfileManager` (singleton pointer used by all engine and game code)

## Overview

Extends the engine's Base/Derived profiling pattern with game-specific CPU counters and timers. Counters track per-frame object counts for each game collection (with rendered subset counts). Timers provide hierarchical CPU timing across the frame update phases (Interpolate, PostRender sub-phases) and render passes, with indented display names reflecting the hierarchy in the profiler overlay.

## Architecture

Follows the engine's ProfileManager Base/Derived pattern: `game::ProfileManager` inherits from `engine::ProfileManagerBase`, adding game-specific counter and timer arrays. Virtual dispatch routes to the correct array based on index. The constructor sets up the `gpProfileManager` global and starts boot timing when profiling is enabled.

`FormatGameScreens()` is overridden (client-only) to render the Frames and Network overlay text screens. Network state (clock correction, reconcile counters) is fed in via `SetClockCorrection()` and `SetReconcileCounters()`, which store smoothed values for display. Network smoothing members and `InTheLastSecond` rate counters live in the game-derived class, not the engine base.

`RenderImPlotGraphs()` is overridden in `NetworkGraphs.cpp` (client-only) to render real-time time-series plots alongside the Network text overlay. Five plots are shown (RTT, jitter, rollback, server buffer, clock error) using `Smoothed<T>` circular buffers as their data source. The plots are displayed in an anchored ImGui window covering the right half of the screen.

## See Also
- Engine profiling base: [../../../../Engine/Source/Profile/CLAUDE.md](../../../../Engine/Source/Profile/CLAUDE.md)
