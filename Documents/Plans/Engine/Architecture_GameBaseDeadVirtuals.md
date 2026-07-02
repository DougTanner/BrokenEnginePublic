# Architecture: GameBase Dead Virtuals & Server MenuInput Plumbing

## Context
Source: /external-architecture-review on Engine/Source (recursive). Two cosmetic-abstraction findings on the engine↔game seam: pure virtuals on `GameBase` that the engine never calls (widening the engine interface without engine need), and a structurally dead server-side input path that fakes a working save/load trigger.

## Design

### Engine/Source/GameBase.h
- Delete `ReplayFile()` (GameBase.h:152) — **zero call sites anywhere in the repo** (only the declaration and the game override at `Projects/.../Game.h:172`); delete the override too [~5m]
- Move `Reset()` and `QuicksaveFile()` (GameBase.h:158-159) down to `game::Game` — zero engine callers; only game-layer code calls them through the base (`GameSaveLoad.cpp:37,45,53,58,72,130,144,220`; `ClientSession.cpp:294`). Contrast `ShouldTrapCursor` etc., which are engine-consumed and stay [~15m]

### Server MenuInput plumbing (structurally dead)
- `Main.cpp:270-284` passes an always-default `menuInput` into `ServerUpdate`, which threads it to `Quickload`/`Quicksave` (`GameBase.cpp:100,157`) whose gating flags no server-build code ever sets (the only fill site is client-only `UpdateMenuInput`; server WndProc sets only `sbQuit`). Real server entry points exist separately (`ServerSave`/`ServerLoad`, `GameSaveLoad.cpp:42,48`). Drop the parameter from `ServerUpdate`/`Quickload`/`Quicksave` (or wire a real trigger if server hotkey save/load is wanted) [~30m]

## Critical files
- `Engine/Source/GameBase.h`, `GameBase.cpp`, `Engine/Source/Main.cpp`
- `Projects/BrokenEngineSandbox/Source/Game.h`, `GameSaveLoad.cpp`, `ClientSession.cpp` (call-site retargets to `game::Game`)

## Out of scope
- The render-clock extraction and defensive log loops in `GameBase::Render`/tick paths (`Engine/Refactor_RootFilesQuickWins.md`)
- Any change to the actual save/load implementations
- Other `GameBase` virtuals (all verified engine-consumed)

## Notes
- Invariant exposure: none — interface reshaping with compile-checked call sites; no wire/CRC/save-format change. Both builds must compile (the `menuInput` parameter removal touches the shared `ServerUpdate` signature — check the client build's calls too)
- Grill decision: server `Quickload`/`Quicksave` — delete the dead parameter path (recommended) vs wire a real server trigger
