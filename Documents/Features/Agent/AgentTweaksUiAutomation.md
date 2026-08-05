# Agent Tweaks UI Automation

## Context

Runtime verification of the Tweaks quick-win refactor on 2026-07-18 exposed an agent-harness capability gap. F3 visibly opened and closed the Tweaks windows, but `describe_ui` and `describe_scene` reported `uiState: "kNone"`, and `click {"label":"Low","window":"Water"}` failed because no matching widget was registered. The current acceptance had to use a persisted-settings fixture instead of driving the tabs; the user approved that replacement and explicitly requested this follow-up.

The verified reporting boundary causing the gap is:

- `BuildDescribeUi` reads `gpGame->meUiState` (`Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp:466`), and `CommandDescribeScene` does the same (`Projects/BrokenEngineSandbox/Source/Agent/AgentScene.cpp:192`). F3 instead toggles `Game::mbShowImGui` (`Projects/BrokenEngineSandbox/Source/Game.cpp:771-774`). `UiState::kTweaks` already exists (`Game.h:28-37`) but is not reached by this overlay path. `Game::ShouldShowInGameUi` (`Game.cpp:539-542`) establishes that `mbShowImGui` takes precedence while the Tweaks overlay is visible.
- Label-addressable tab-item repair is owned by the executable [AgentHarness ImGui tab item addressing Plan](../../Plans/Agents/AgentHarnessImGuiTabSelection.md). This Feature covers the separate effective UI-state reporting gap and does not own the registry hook-order or tab-selection fix.

The effective UI-state reporting defect is the scope of this Feature. The linked executable Plan owns tab-label and tab-selection repair, so the two changes have separate implementation and landing boundaries.

## Design

1. Give the game agent reporting path one consistent effective UI-state rule: while `Game::mbShowImGui` is true, both `describe_ui` and `describe_scene` report `kTweaks`; otherwise they report the existing `meUiState`. Ground the precedence in `Game::ShouldShowInGameUi` and avoid changing the production UI state machine merely to satisfy reporting.
2. Update `.agents/skills/agent-harness/SKILL.md` so the `describe_ui` and `describe_scene` contracts document the effective Tweaks state. Tab-item addressing documentation belongs to the linked executable Plan.

This is a Tier 3 change because it changes agent JSON reporting at the loopback command trust boundary.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp` — `BuildDescribeUi` effective `uiState` reporting.
- `Projects/BrokenEngineSandbox/Source/Agent/AgentScene.cpp` — `CommandDescribeScene` effective `uiState` reporting.
- `Projects/BrokenEngineSandbox/Source/Game.h` and `Game.cpp` — `UiState::kTweaks`, `mbShowImGui`, and `ShouldShowInGameUi` authority for overlay precedence.
- `.agents/skills/agent-harness/SKILL.md` — effective Tweaks state reporting contract; tab-item addressing remains owned by the linked executable Plan.

## Out of scope

- Production UI state-machine changes or assigning `meUiState = kTweaks` for F3.
- AgentUiRegistry hook-order handling, tab-label discovery, and tab selection; those repairs belong to the linked executable Plan.
- The current `BeginSubtab` Tweaks refactor or other Tweaks implementation changes.
- Generic screenshot vision, pixel-coordinate discovery, or pixel guessing.
- Modifications to vendored ImGui.
- Unrelated Tweaks slider-map warnings.
- New agent commands or Tweaks-specific command parameters.

## Acceptance criteria

- Debug client and server compile through `/compile`.
- In a harness-driven Debug client, F3 opens Tweaks and both `describe_ui` and `describe_scene` report `uiState: "kTweaks"`; F3 closes it and both return to the underlying `meUiState` value.
- The effective-state wording in the agent-harness contract matches the reporting behavior.
- C++ correctness, affected-code, and style reviews cover the engine/game changes; the changed skill passes `/validate-skill` and its fresh coherence review.

## Notes

- Agent loopback JSON behavior changes: `uiState` more accurately reports an already-visible overlay. Gameplay network/wire protocol is unchanged.
- Client-only UI and agent reporting paths do not touch deterministic simulation, CRC state, replay/save compatibility, `kiVersion`, `.pack` data, serialization layout, shaders, or server runtime behavior. The server build remains an acceptance check because shared sources and guards compile in both targets.
- No new source files are expected; project membership should remain unchanged.
