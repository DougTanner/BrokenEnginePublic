# Connection-loss modal feedback on mid-session disconnect

## Context

Follow-up residual from `Documents/Plans/Engine/Bugfix_RenderFrameEmptySnapshotRingOnReconnect.md` (execution step 6 / its `## Out of scope`). That bugfix stopped the client crashing on a failed reconnect / mid-session server death, and agent-harness verification confirmed the client now returns **cleanly to the main menu** (`gameFlags:["kMainMenu"]`, `uiState:"kPause"`) — but with **no user feedback**: the player is not told the connection was lost. The bugfix deferred this to a follow-up rather than expand its scope.

Investigation of the modal wiring (2026-07-10): the game `ModalScreen` (`game::ModalScreen::Render`, gated on `gpGame->meUiState == UiState::kModal`, text from `char Game::mModalMessage[256]`; OK button → `meUiState = kPause` + clears the message) is **already reused** for three failure modes, so the modal system exists and does not need building:

1. **Connect-time rejection with a server reason** — `ClientSession::PollConnectionStatus` (`ClientSession.cpp:263-270`): `Client::GetRejectionReason()` (the `ServerConnectionResponse` reject string in `Client::mpcRejectionReason`, filled in `ClientReceive.cpp:437-440`) → `snprintf` into `mModalMessage`, disconnect, `meUiState = kModal`.
2. **Connect-time failure** — `PollConnectionStatus` (`:272-278`): `Client::WasDisconnected()` while not yet accepted → `"Connection failed"` modal.
3. **Desync escalation** — `ClientDesyncManager` (`:47`, `:70`, `:96`): sets `"Desynced from server"` into `mModalMessage` **before** calling `Disconnect()`, so the message is already non-empty when the disconnect is later observed.

The one **uncovered** mode is a clean mid-session disconnect (server death / ENet timeout after the session was accepted): `ClientSession::PollConnection` (`:318-321`) runs

```
gpGame->ChangeFrame(GameFlags::kMainMenu);
gpGame->meUiState = gpGame->mModalMessage[0] != '\0' ? UiState::kModal : UiState::kPause;
```

On a plain server death `mModalMessage` is **empty** (cleared at connect in `ConnectToServer` `:237`; nothing sets it during a healthy session) → the ternary picks `kPause`, a silent menu return. This exactly matches the harness observation (`uiState:"kPause"`).

**Disconnect reason availability: none.** The server always calls `enet_peer_disconnect(peer, 0)` (`Server.cpp:537`) / `enet_peer_disconnect_later(peer, 0)` (`ServerReceive.cpp:263/276`) with `data 0`, and the client `ENET_EVENT_TYPE_DISCONNECT` handler (`Client.cpp:129-134`) ignores `event.data` entirely. The only structured reason on the wire is the connect-time `ServerConnectionResponse` rejection string; there is no per-cause code for a mid-session drop. So the mid-session case can only surface a **generic** message ("connection lost").

Note the `LOCAL SERVER` menu button is discovery-gated (disabled `SCANNING...` with no server discovered), so the reachable user-visible failure is exactly this mid-session death → menu-return path, not a from-cold connect attempt.

## Design

In `ClientSession::PollConnection`, at the `WasDisconnected()` branch (`:318-321`): when `mModalMessage` is empty, populate a generic default (e.g. `"Connection lost"` / `"Disconnected from server"`) **before** the `kModal`/`kPause` selection, so a clean mid-session disconnect surfaces the existing modal like the other three failure modes. When a more-specific message is already set (desync, or a connect-time reason that reached this branch), leave it untouched — the specific message wins.

- Reuse the existing `ModalScreen` entirely — **no new modal type**, no change to `ModalScreen::Render`, no new `UiState`.
- Result: 4-of-4 failure modes surface the modal (currently 3-of-4).
- String: an English literal matches the existing `"Connection failed"` / `"Desynced from server"` literals and is the KISS choice; optionally route through the `Localization` table like the menu strings (single open question for grill).

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp`
  - `ClientSession::PollConnection` — the mid-session `Client::WasDisconnected()` branch (`:318-321`): default-message population when `mModalMessage` is empty. Sole edit site.
  - Reference (unchanged): `ClientSession::PollConnectionStatus` connect-time rejection / `"Connection failed"` modal (`:259-283`); `ClientSession::ConnectToServer` message clear (`:237`).
- Reference-only (no change):
  - `Projects/BrokenEngineSandbox/Source/Ui/Screens/ModalScreen.cpp` — `ModalScreen::Render` reads `mModalMessage`/`meUiState`, OK → `kPause` + clear.
  - `Projects/BrokenEngineSandbox/Source/Game.h` — `mModalMessage[256]` (`:145`), `UiState::kModal` (`:33`).
  - `Projects/BrokenEngineSandbox/Source/Network/Client/ClientDesyncManager.cpp` — desync message precedent (message set before `Disconnect()`).
  - `Engine/Source/Network/Client/Client.h` — `WasDisconnected()` / `GetRejectionReason()` (`:114-115`), `mpcRejectionReason` (`:189`).

## Out of scope

- **Plumbing a structured disconnect reason** (server-closed vs. timeout vs. crash) — ENet carries none on a mid-session disconnect (`data 0`), so the message is necessarily generic. Adding a reason code is a wire/protocol change needing a `kuiProtocolVersion` bump — not this plan.
- The three **already-wired** modal paths (connect-time rejection, connect-time `"Connection failed"`, desync) — verified present; unchanged.
- `ModalScreen` appearance / layout — defined by `Documents/UserInterfaceDesign.txt`.
- Reconnect / subscription-lifecycle correctness (why the ring never populated) — `Network/SubscriptionLifecycleRaceHardening.md` / `Network/ClientFullStateEdgeFixes.md`.
- Distinguishing a future user-initiated disconnect from a server death (no user-initiated disconnect UI exists today; if one is added it should suppress this modal).

## Acceptance criteria

- After connecting to a live local server and killing the server mid-session, the client shows `ModalScreen` with a generic connection-lost message (`uiState:"kModal"`) instead of silently returning to `kPause`.
- Clicking OK dismisses to `kPause` at the main menu (existing `ModalScreen` OK behavior); `mModalMessage` is cleared.
- The connect-time rejection, connect-time `"Connection failed"`, and desync modals are unchanged — their specific messages still win over the generic default.
- Client-only ImGui/UI state; `mModalMessage`/`meUiState` are not CRC'd, serialized, or version-gated. No CRC/determinism/`kiVersion`/`.pack`/replay/wire exposure; all edits stay inside the existing `#if defined(BT_CLIENT)` `ClientSession` path.

## Notes

- **Invariant exposure**: none — client-only UI state. No architectural decision.
- **Grill (single open question)**: English literal (KISS, matches existing modal strings) vs. `Localization`-table string.
