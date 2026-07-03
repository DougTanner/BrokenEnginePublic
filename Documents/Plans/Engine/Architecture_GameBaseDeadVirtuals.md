# Architecture: GameBase Dead Virtuals & Server MenuInput Plumbing

## Context
Source: /external-architecture-review on Engine/Source (recursive), re-verified against source. Two cosmetic-abstraction findings on the engine↔game seam: pure virtuals on `GameBase` that the engine never calls (widening the engine interface without engine need), and a structurally dead server-side input path that fakes a working save/load trigger.

## Design

### Engine/Source/GameBase.h
- Delete `ReplayFile()` (GameBase.h:159) — **zero call sites anywhere in the repo** (only the declaration and the game override at `Projects/.../Game.h:172`); delete the override too [~5m]
- Move `Reset()` (GameBase.h:152) and `QuicksaveFile()` (GameBase.h:158) down to `game::Game` — zero engine callers; only game-layer code calls them through the base (`GameSaveLoad.cpp:37,45,53,58,72,130,144,220` via its `mrGameBase` reference; `ClientSession.cpp:294`). Retarget those call sites to `game::Game` (or give `GameSaveLoad` a `game::Game&`). Contrast `ShouldTrapCursor` etc., which are engine-consumed and stay [~15m]

### Server MenuInput plumbing (structurally dead — verified)
- `Main.cpp:270-284` passes an always-default `menuInput` into `ServerUpdate`, which threads it to `Quickload`/`Quicksave` (`GameBase.cpp:100,157`) whose gating flags (`kQuicksave`/`kQuickload`/`kResetFrame`) no server-build code ever sets: the only fill site is `game::Input::UpdateMenuInput` (client-only at runtime — `gpInput` is nullptr on the server and `ProcessInput` is called only in Main.cpp's `BT_CLIENT` block); the server WndProc sets only `sbQuit` (F4). The real server entry points are the network-request handlers — client F5/F6/Enter send `GamePacketType::kClientSaveRequest`/`kClientLoadRequest`/`kClientResetRequest` (`Game.cpp:736-755`), which the server routes to `ServerSave`/`ServerLoad`/`ServerReset` (`GameSaveLoad.cpp:42,48,66`). Dropping the parameter makes the `Quickload`/`Quicksave` bodies unreachable in full — delete both functions and the `ServerUpdate` sites (or wire a real trigger if server-local save/load is wanted) [~30m]

## Critical files
- `Engine/Source/GameBase.h`, `GameBase.cpp`, `Engine/Source/Main.cpp`
- `Projects/BrokenEngineSandbox/Source/Game.h`, `GameSaveLoad.{h,cpp}`, `ClientSession.cpp` (call-site retargets to `game::Game`)

## Out of scope
- The render-clock extraction and defensive log loops in `GameBase::Render`/tick paths (`Engine/Refactor_RootFilesQuickWins.md`)
- Any change to the actual save/load implementations (`ServerSave`/`ServerLoad`/`ServerReset` and their network triggers are untouched)
- Other `GameBase` virtuals (all verified engine-consumed)

## Notes
- Invariant exposure: none — interface reshaping with compile-checked call sites; no wire/CRC/save-format change. `ServerUpdate` is `BT_SERVER`-only, so the parameter removal is server-build-only; Main.cpp's `menuInput` local stays for the client `ProcessInput` path — gate its declaration (or mark it) so the server build doesn't warn on an unused local
- Grill decision: server `Quickload`/`Quicksave` — delete the dead parameter path (recommended) vs wire a real server trigger

## Verification Notes
- All claims re-verified: `ReplayFile` has zero call sites repo-wide; `Reset`/`QuicksaveFile` have zero engine callers (game callers exactly as cited); the server MenuInput path is unreachable as described.
- **Conflict with live plan:** `Network/ServerPauseAndResetSemantics.md` item (b) completes the `kResetFrame` fresh-game branch inside `GameSaveLoad::Quickload` (:152-157) and presumes that "local debug-menu" path is reachable — on the server build it is not (this plan's finding). Resolve jointly at grill: deleting the MenuInput path obsoletes that plan's local-menu half (its networked `ServerReset` half is unaffected); wiring a real server trigger instead makes its completion meaningful. Co-schedule or sequence the two plans.
