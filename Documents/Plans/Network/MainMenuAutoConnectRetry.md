<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-07T13:47:46.486Z","dependsOn":[]} -->
# Main menu auto-connect never retries after a failed connection

## Context

`MainMenuScreen::Render()` gates automatic connection behind a function-local process-lifetime latch: `static bool sbConnected` is set true immediately before `ConnectToDiscoveredServer()` and never reset (`Projects/BrokenEngineSandbox/Source/Ui/Screens/MainMenuScreen.cpp:95-103`). `kbAutoConnect` is enabled in Debug and Profile builds (`Projects/BrokenEngineSandbox/Source/Pch.h:33,51`).

Verified failure path: a rejected or failed connection reaches `ClientSessionRuntime::Disconnect()`, which destroys the client and clears discovery state (`Engine/Source/Network/Client/ClientSessionRuntime.cpp:115-127`, invoked from `PollAndDrain` at `:180-196`); the modal returns the UI to the main menu (`ModalScreen.cpp`), discovery restarts (`MainMenuScreen.cpp:64-67`) and re-sets `kServerDiscovered` (`ClientSessionRuntime.cpp:148`) — but the permanent latch suppresses every later automatic attempt, so auto-connect is dead until process restart and the developer must connect manually.

Verified safety constraint: deleting only the latch is unsafe. `ConnectToDiscoveredServer()` clears `kServerDiscovered` before connecting (`ClientSessionRuntime.cpp:109-113`), but discovery polling runs before client polling and can re-set `kServerDiscovered` while a client is active (`ClientSessionRuntime.cpp:170-173,144-148`), and the runtime exposes no connecting flag — only `mpClient` and the discovery flags (`Engine/Source/Network/Client/ClientSessionRuntime.h`). An unlatched auto-connect could therefore replace an in-progress or connected client after a rediscovery.

Origin: /external-deep-analysis of `MainMenuScreen.cpp` (architecture review Lens B+D finding, verified with the safety caveat above by the Phase-3 verification reviewer). Pre-existing debt, outside any active change.

## Design

Gate the main-menu session-lifecycle block on the absence of a live client instead of a process-lifetime latch, inside `MainMenuScreen::Render()`:

- Delete `static bool sbConnected` entirely.
- Wrap the discovery-autostart condition (`MainMenuScreen.cpp:64`) and the `kbAutoConnect` block (`:95-103`) so both run only when `gpClientSession->mpRuntime->mpClient == nullptr`. With that gate, one automatic attempt per fresh discovery is already guaranteed by the existing runtime state machine: connecting clears `kServerDiscovered`, and a failed attempt destroys the client and re-enables the gate.
- The `kbAutoRunServer` block and its `sbServerLaunched` latch are untouched.

No engine change: retry state stays UI-side and derives from runtime-owned `mpClient`/`kServerDiscovered`; no new member, flag, or API is added. Rendered output is unchanged: while a client exists, `kServerDiscovered` is false today (cleared at connect), so the Local Server button already shows the disabled SCANNING state either way, and no label, geometry, theme, or navigation changes (frozen per `Documents/UserInterfaceDesign.txt`; `describe_ui` label set must stay identical).

## Critical files

- `Projects/BrokenEngineSandbox/Source/Ui/Screens/MainMenuScreen.cpp`

## In scope

- `MainMenuScreen::Render()` lines 63-103 only: the discovery-autostart condition, removal of `sbConnected`, and the `mpClient == nullptr` gate around the auto-connect block.

## Out of scope

- Any change to rendered layout, labels, theming, navigation, or screen transitions.
- The `kbAutoRunServer` block and `sbServerLaunched`.
- The manual Local Server / Remote Server button handlers.
- `ClientSessionRuntime`, `ClientSession`, and every engine Network file.
- Decomposition or other restructuring of `Render()` (evaluated separately and rejected as metric-only).

## Risk tier and invariants

Change Workflow Tier 2 — scoped client runtime behavior in one subsystem; no determinism/CRC, wire/protocol, serialization, threading, or trust-boundary exposure (all main-thread client UI). Escalate only if implementation is forced outside the In scope boundary.

## Acceptance criteria

- With `kbAutoConnect` enabled and no client, each fresh server discovery triggers exactly one automatic connection attempt — including after a prior failed or rejected attempt (live /agent-harness scenario: fail one connect, allow rediscovery, observe the automatic retry in a Debug or Profile client).
- While a client exists, a completed rediscovery never triggers an automatic `ConnectToDiscoveredServer()` from the main menu.
- Main-menu `describe_ui` label set and before/after screenshots are identical.

## Notes

- Verification caveat from the analysis: the reviewer could not fully confirm from static evidence that connection failure/rejection presents the modal while `InMainMenu()` stays true; the harness scenario above settles it live. If the modal path proves unreachable, re-verify the retry path via whatever UI state the failure actually lands in before implementing.
- Retained analysis evidence: `Temp/arch-lens-b-out.md`, `Temp/arch-lens-d-out.md`, `Temp/verify-findings-out.md` (machine-local, not tracked).
