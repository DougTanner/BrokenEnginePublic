# Architecture: Gamepad Disconnect Snapshot Staleness

## Context

Source: /external-deep-analysis on `Engine/Source/Input`. The disconnect branch of `RawInputManager::Update`
(`RawInputManager.cpp:192-213`) clears thumbsticks and all eight gamepad buttons but not `f2Dpad`/`f2Triggers`
(written in the connected branch at `:177-181`). `Input/CLAUDE.md:21` documents the asymmetry as known but does
not assess it; assessment from this run:

- **`f2Dpad` staleness is observable**: game `Input.cpp:99-102` pushes `f2Dpad` into ImGui dpad navigation
  whenever a menu is open. If the pad disconnects while a dpad direction is held and a menu is up, ImGui sees
  that direction held indefinitely until reconnect.
- **`f2Triggers` is a dead field**: written at `RawInputManager.cpp:180-181`, zero consumers repo-wide
  (grep-verified this run).

## Design

### Engine/Source/Input/RawInputManager.cpp
- In the disconnect branch of `RawInputManager::Update` (`:192-213`), zero `mRawInput.f2Dpad` alongside the
  existing thumbstick/button clears [~5m]

### Engine/Source/Input/RawInputManager.h + RawInputManager.cpp
- Delete the dead `RawInput::f2Triggers` member (`RawInputManager.h:47`) and its two writes
  (`RawInputManager.cpp:180-181`). Re-verify zero consumers via grep at execution — the struct sits outside the
  `BT_CLIENT` guard and game `Input.h:3` includes the header in both builds, so the removal is compile-checked
  client + server [~5m]

### Engine/Source/Input/CLAUDE.md
- Update the disconnect-asymmetry sentence (`:21`, "triggers/dpad retain stale values") to match the fixed
  behavior [~5m]

## Critical files

- `Engine/Source/Input/RawInputManager.h`
- `Engine/Source/Input/RawInputManager.cpp`
- `Engine/Source/Input/CLAUDE.md`

## Out of scope

- The mouse `RIDEV_INPUTSINK` registration question — owned by `Engine/DeadCodeAndUnusedIncludesSweep.md` item 8.
- The stale `RawInputManager.cpp:42` INPUTSINK comment — owned by `Common/StaleCodeCommentsSweep.md`.
- Connected-branch behavior, vibration passthrough, focus handling — unchanged.
- Any change to how game `Input.cpp` consumes the dpad — the fix is producer-side only.

## Notes

- No determinism/CRC/replay exposure: `RawInput` is a client-local hardware snapshot; the deterministic
  `FrameInput` machinery is minted game-side from serialized commands (verified this run — see game
  `Input/CLAUDE.md`).
- `f2Dpad` zeroing is behavior-visible only in the disconnect-while-menu-open corner; no playtest beyond a
  pad-unplug sanity check.

## Verification Notes

- Verification pass (this run) confirmed every citation against source: disconnect branch
  `RawInputManager.cpp:192-213`; connected-branch `f2Dpad`/`f2Triggers` writes `:177-181`; `f2Triggers` decl
  `RawInputManager.h:47` with zero consumers repo-wide (only the two writes at `:180-181`); `RawInput` struct
  outside the `BT_CLIENT` guard (guard opens `RawInputManager.h:50`), so the removal is compile-checked in
  both builds via game `Input.h:3`; ImGui dpad feed at game `Input.cpp:99-102` is `f2Dpad`'s only consumer;
  `Input/CLAUDE.md:21` wording matches. Out-of-scope routing confirmed (`DeadCodeAndUnusedIncludesSweep` item
  8 owns the mouse `RIDEV_INPUTSINK` question; `StaleCodeCommentsSweep` owns the `:42` comment).
- Supersedes `Input/Architecture_GamepadDisconnectClear.md` (earlier-run duplicate covering the same
  disconnect branch, deleted at verification) — its only delta, zeroing `f2Triggers` on disconnect, is
  obsoleted by this plan's deletion of the dead field.
- Coordinate with `Input/Refactor_InputButtonFlags.md` if co-scheduled — it restructures the same
  `RawInput` struct and disconnect clear block (different concern, shared lines).
- No corrections required; no items dropped.
