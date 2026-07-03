# Architecture: GameBase Dead Virtuals & Server MenuInput Plumbing

## Context
Source: /external-architecture-review on Engine/Source (recursive), re-verified against source. Two cosmetic-abstraction findings on the engine↔game seam: pure virtuals on `GameBase` that the engine never calls (widening the engine interface without engine need), and a structurally dead server-side input path that fakes a working save/load trigger.

## Design

### Engine/Source/GameBase.h
- Delete `ReplayFile()` (GameBase.h:159) — **zero call sites anywhere in the repo** (only the declaration and the game override at `Projects/.../Game.h:172`); delete the override too [~5m]
- Move `Reset()` (GameBase.h:152) and `QuicksaveFile()` (GameBase.h:158) down to `game::Game` — zero engine callers; only game-layer code calls them through the base (`GameSaveLoad.cpp:37,45,53,58,72,130,144,220` via its `mrGameBase` reference; `ClientSession.cpp:294`). Retarget those call sites to `game::Game` (or give `GameSaveLoad` a `game::Game&`). Contrast `ShouldTrapCursor` etc., which are engine-consumed and stay [~15m]

### Server MenuInput plumbing (structurally dead — verified)
- `Main.cpp:270-284` passes an always-default `menuInput` into `ServerUpdate`, which threads it to `Quickload`/`Quicksave` (`GameBase.cpp:100,157`) whose gating flags (`kQuicksave`/`kQuickload`/`kResetFrame`) no server-build code ever sets: the only fill site is `game::Input::UpdateMenuInput` (client-only at runtime — `gpInput` is nullptr on the server and `ProcessInput` is called only in Main.cpp's `BT_CLIENT` block); the server WndProc sets only `sbQuit` (F4). The real server entry points are the network-request handlers — client F5/F6/Enter send `GamePacketType::kClientSaveRequest`/`kClientLoadRequest`/`kClientResetRequest` (`Game.cpp:736-755`), which the server routes to `ServerSave`/`ServerLoad`/`ServerReset` (`GameSaveLoad.cpp:42,48,66`). Dropping the parameter makes the `Quickload`/`Quicksave` bodies unreachable in full — **Decision (2026-07-03): delete, do not wire a trigger.** Delete `GameSaveLoad::Quicksave` and `GameSaveLoad::Quickload` entirely (bodies `GameSaveLoad.cpp:27-40,115-164`, declarations `GameSaveLoad.h:19-20`), remove the `mGameSaveLoad.Quickload(...)` early-return block (`GameBase.cpp:125-129`) and the `mGameSaveLoad.Quicksave(...)` call (`GameBase.cpp:182`), and drop the `MenuInput` parameter from `ServerUpdate` (`GameBase.h`/`GameBase.cpp:119`, call site `Main.cpp:284`). The headless server has no local input source to wire (WndProc sets only `sbQuit`), and the networked request path already provides all three operations — a server-local trigger would be YAGNI [~30m]

## Critical files
- `Engine/Source/GameBase.h`, `GameBase.cpp`, `Engine/Source/Main.cpp`
- `Projects/BrokenEngineSandbox/Source/Game.h`, `GameSaveLoad.{h,cpp}`, `ClientSession.cpp` (call-site retargets to `game::Game`)

## Out of scope
- The render-clock extraction and defensive log loops in `GameBase::Render`/tick paths (`Engine/Refactor_RootFilesQuickWins.md`)
- Any change to the actual save/load implementations (`ServerSave`/`ServerLoad`/`ServerReset` and their network triggers are untouched)
- Other `GameBase` virtuals (all verified engine-consumed)

## Notes
- Invariant exposure: none — interface reshaping with compile-checked call sites; no wire/CRC/save-format change. `ServerUpdate` is `BT_SERVER`-only, so the parameter removal is server-build-only; Main.cpp's `menuInput` local stays for the client `ProcessInput` path — gate its declaration (or mark it) so the server build doesn't warn on an unused local
- Decision (2026-07-03): server `Quickload`/`Quicksave` — **delete the dead parameter path** (decided in Design above; wiring a server trigger rejected as YAGNI — the headless server has no local input source and the networked `kClientSaveRequest`/`kClientLoadRequest`/`kClientResetRequest` path fully covers save/load/reset)

## Verification Notes
- All claims re-verified: `ReplayFile` has zero call sites repo-wide; `Reset`/`QuicksaveFile` have zero engine callers (game callers exactly as cited); the server MenuInput path is unreachable as described (additionally the `Quicksave`/`Quickload` bodies are `if constexpr (kbDebugInput)`-gated, `GameSaveLoad.cpp:33,121`).
- **Cross-plan resolution (2026-07-03):** the former conflict with `Network/ServerPauseAndResetSemantics.md` is resolved — that plan **dropped** its item (b) (completing the `kResetFrame` fresh-game branch inside `GameSaveLoad::Quickload:152-157`) as moot, deferring to this plan's deletion of the whole MenuInput path; its networked pause-preservation work is unaffected. Ownership: **this plan owns every `GameSaveLoad::Quicksave`/`Quickload` and `ServerUpdate`-signature edit**; that plan owns the `ServerBroadcaster.cpp`/`ServerSession.cpp` pause-gating edits. No shared edit sites; the two plans can execute in either order (if this one runs second, the dead branch simply waits as-is).
