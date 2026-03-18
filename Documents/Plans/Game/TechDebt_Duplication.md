# Tech Debt: Code Duplication

Source: /external-tech-debt on Projects/BrokenEngineSandbox/Source

## Changes

### Projects/BrokenEngineSandbox/Source/Game.cpp
- Extract frame initialization helper from duplicated logic at lines 254-258 and 442-446 (postRender.uiFrameId, randomEngine.TimeSeed, playerAlignment, enemyAlignment, alignments). Create a private `InitFramePostRender(Frame& rFrame)` method called from both `CreateFrameAtCoord()` and `CreateNewFrame()` [~15m]

### Projects/BrokenEngineSandbox/Source/Game.h + Game.cpp
- Consolidate music start pattern: `StartGameMusic()` (Game.h:152-157) resets index and plays track 0; same pattern in constructor (Game.cpp:44) and `ChangeFrame()` (Game.cpp:481-491). Extract a `StartMenuMusic()` counterpart and use both in constructor and `ChangeFrame()` [~15m]

## Verification Notes
- Frame init helper: Verified — 5 identical lines at lines 254-258 and 442-446
- Music consolidation: Main value is replacing inline code in `ChangeFrame()` else-branch (lines 489-490) with `StartGameMusic()`. Constructor case saves only one line
