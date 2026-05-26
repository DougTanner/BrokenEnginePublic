# Architecture: Relocate GameSaveLoad to the Game Layer

## Context

`Engine/Source/GameSaveLoad.{h,cpp}` declares `engine::GameSaveLoad` but is entirely game-specific — the whole class body is wrapped in `#if defined(BT_SERVER)` and it traffics exclusively in game types:

- `game::gpGame->mClientGridCoord` / `SetClientGridCoord(...)` (`GameSaveLoad.cpp:35, :43, :57, :81, :109, :146, :271, :310`), `game::gpGame->mFrameInputs` (`:275, :327, :339`), `game::gpGame->CreateNewFrame(game::GameFlags::kGame)` (`:68, :130, :139`), `game::gpGame->RestoreReplayMeta` / `ApplyTransferStatusChanges` / `ClientPlayerId` / `PreviousClientArmor` (`:250, :311-312, :346`).
- `game::gpServerSession->mpFleetManager->ResetState()` (`:73, :154`), `ResetClientsForLoad()` (`:58, :74, :148, :251`), `ComputeActiveSet()` (`:59, :75, :252`), `WriteFleetData` / `ReadFleetData` (`:375, :419`).
- `game::MenuInput` / `game::MenuInputFlags` parameters (`Quicksave`, `Quickload`).
- Concrete `game::Frame` / `game::FrameInput` serialization via `DifferenceStreamReader/Writer<game::Frame, game::FrameInput>` (`GameSaveLoad.h:29, :41-42`; `.cpp` reader/writer construction).
- `game::ReplayMeta` (`GameSaveLoad.cpp:181, :309`), `game::Frame::kiVersion` (`:363, :408, :410`).

The class is owned by value by the engine base: `GameSaveLoad mGameSaveLoad;` at `Engine/Source/GameBase.h:215` (inside `#if defined(BT_SERVER)`), constructed `mGameSaveLoad(*this)` at `GameBase.cpp:21`, with `friend class GameSaveLoad;` at `GameBase.h:147`.

**Why this is non-trivial:** `GameBase` (engine) currently OWNS the member AND drives the per-tick save/load flow. `GameBase::ServerUpdate` (`GameBase.cpp:97-166`) calls into `mGameSaveLoad` at many points across the tick:
- `Quickload(rMenuInput)` early-return gate at **line 103**
- `SaveLoadReplay()` at **line 109**
- `IsRecording() / IsReplaying()` + `SyncReplayTick()` inside the per-tick loop at **lines 145-147**
- `TickAutosave()` at **line 164** and `Quicksave(rMenuInput)` at **line 165**

Other engine reads: `mGameSaveLoad.IsReplaying()` at `GameBase.cpp:222, :434, :471` and `GetReplayReaders()` at `GameBase.cpp:440` (the finalize / replay-tick paths), plus `Main.cpp:319` `pGame->mGameSaveLoad.Autosave()`.

The game layer ALSO calls the member directly: `Game.cpp:42` (`Autoload()`), `Game.cpp:751` (`ResetStreams()`), `ServerSession.cpp:42` (`IsReplaying()`), `ServerSession.cpp:181/187/193` (`ServerSave/ServerLoad/ServerReset`). So the member is consumed from both layers today.

## Design

Move the class to the game layer: `Projects/BrokenEngineSandbox/Source/` (a `Save/` or `Network/Server/` sibling — pick the simplest co-location during the grill), `game::` namespace, renamed type `game::GameSaveLoad` (or keep the name; it stops being an `engine::` type). The member moves from `engine::GameBase` to `game::Game`.

The architectural decision the grill MUST resolve is **how the engine per-tick flow in `GameBase::ServerUpdate` calls into the now-game-owned save/load**, because the engine cannot name `game::Game::mGameSaveLoad`. Two candidate shapes (present both to the user with pros/cons):

- **Option A — virtual hooks on `GameBase`.** Add server-only virtuals (`OnServerPreTickSaveLoad()` returning a "skip-tick" bool for the Quickload early-return, `OnServerReplayTick()`, `OnServerPostTickSave()`, plus `IsReplaying()` / replay-reader access) that `GameBase::ServerUpdate` calls at the existing points; `game::Game` overrides them to drive its owned `GameSaveLoad`. Pro: minimal change to the engine tick skeleton; keeps the phase ordering visible in `ServerUpdate`. Con: adds ~4-6 virtuals to the `GameBase` interface; the `IsReplaying()` reads at `:222/:434/:471` and `GetReplayReaders()` at `:440` also need engine-visible accessors.
- **Option B — move the calls into game `ServerSession` / `Game`.** Relocate the `Quickload` gate, `SaveLoadReplay`, `SyncReplayTick`, `TickAutosave`, `Quicksave`, and `Autosave` invocations out of `GameBase::ServerUpdate` / `Main.cpp` into the game `ServerSession` tick entry points (which already call into the session each tick). Pro: removes the engine→save/load dependency entirely rather than re-expressing it as virtuals. Con: larger surgery on `ServerUpdate`; must preserve the exact phase ordering (Quickload early-return BEFORE `SaveLoadReplay` and `WaitForTick`; `SyncReplayTick` AFTER `PrepareTick` and BEFORE `BuildAndDispatchFrameTicks`; autosave/quicksave AFTER the tick loop) — see `GameBase.cpp:97-166` for the canonical order.

The `IsReplaying()` reads inside finalize/replay-harvest (`GameBase.cpp:222` skips transfer harvest during replay; `:434/:440` drive the replay-reader iteration; `:471`) are determinism-load-bearing (transfer harvest is skipped during replay for deterministic reproduction — see `Engine/Source/CLAUDE.md` Finalize note). Whichever option is chosen, these reads need a clean engine-visible signal (a `bool mbReplaying` mirrored on `GameBase`, or a virtual `IsReplaying()`); decide in the grill.

Fleet persistence already delegates to `game::ServerSession::WriteFleetData/ReadFleetData` (per `Engine/Source/CLAUDE.md`), so that boundary is clean and does not change.

## Critical files

- `Engine/Source/GameSaveLoad.{h,cpp}` — the class being relocated (delete from `engine::`, recreate in `game::`).
- `Engine/Source/GameBase.h` — `mGameSaveLoad` member (line 215), `friend class GameSaveLoad` (line 147), `#include "GameSaveLoad.h"` (line 4); add the chosen seam (virtuals or replay signal).
- `Engine/Source/GameBase.cpp` — ctor init `:21`; all `mGameSaveLoad.*` call sites: `:103, :109, :145, :147, :164, :165, :222, :434, :440, :471`. Rewrite per chosen option.
- `Engine/Source/Main.cpp:319` — `pGame->mGameSaveLoad.Autosave()`.
- `Projects/BrokenEngineSandbox/Source/Game.{h,cpp}` — new `GameSaveLoad` member owner; existing direct calls at `Game.cpp:42, :751`.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — existing direct calls at `:42, :181, :187, :193`.
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandboxServer.vcxproj` (+ `.filters`) — move the `GameSaveLoad.{h,cpp}` entries (currently at `.vcxproj:334, :429` and `.filters:357, :605`) to the new game-layer paths/filters.
- `Engine/Source/CLAUDE.md` — the "Save / Load / Replay (server-only)" section documents `GameSaveLoad.cpp`; update if the file relocates.

## Out of scope

- **Fleet (de)serialization.** `WriteFleetData` / `ReadFleetData` stay in `game::ServerSession`; this plan does not touch fleet persistence beyond the existing delegated calls.
- **The save-file binary format / version.** `game::Frame::kiVersion` checks and the on-disk layout are unchanged — this is a relocation, not a format migration. (Format migration is a separate concern tracked elsewhere.)
- **DifferenceStream reader/writer internals** (`Engine/Source/File/`) — they already template on the game types and are consumed, not modified.
- **Client-side save/load.** The class is `BT_SERVER`-only; the client build is unaffected.

## Acceptance criteria

- `Engine/Source/` contains no `GameSaveLoad.{h,cpp}` and no `engine::GameSaveLoad` references; the class lives under `Projects/BrokenEngineSandbox/Source/` in `game::`.
- `GameBase::ServerUpdate` preserves the exact save/load phase ordering documented in `Engine/Source/CLAUDE.md` (pre-tick net → quickload early-return → save/load replay → wait → per-tick prepare/replay-sync/dispatch/finalize → resends → autosave/quicksave).
- Quicksave/quickload (F-key, `kbDebugInput`), autosave-on-interval, ServerSave/Load/Reset, and replay record/playback all function identically in a local server session.
- Replay determinism is preserved: transfer harvest is still skipped during replay (the `IsReplaying()` gate at the former `GameBase.cpp:222` retains its meaning through the new seam).
- Server vcxproj builds with the relocated file in its new filter; no engine-project reference to the moved file remains.
