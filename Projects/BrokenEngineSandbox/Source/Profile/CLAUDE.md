# /Projects/BrokenEngineSandbox/Source/Profile/

Game-specific profiling that extends `engine::ProfileManagerBase` with game counters and timers.

**Global**: `game::gpProfileManager` (singleton pointer used by all engine and game code)

## Overview

Extends the engine's Base/Derived profiling pattern with game-specific CPU counters and timers. Counters track per-frame object counts for each game collection (with rendered subset counts). Timers provide hierarchical CPU timing across the frame update phases (Interpolate, PostRender sub-phases) and render passes, with indented display names reflecting the hierarchy in the profiler overlay.

## Architecture

Follows the engine's ProfileManager Base/Derived pattern: `game::ProfileManager` inherits from `engine::ProfileManagerBase`, adding game-specific counter and timer arrays. Virtual dispatch routes to the correct array based on index. The constructor sets up the `gpProfileManager` global and starts boot timing when profiling is enabled.

## See Also
- Engine profiling base: [../../../../Engine/Source/Profile/CLAUDE.md](../../../../Engine/Source/Profile/CLAUDE.md)
