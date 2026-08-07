<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-07T20:13:51.490Z","dependsOn":[]} -->
# Document that any later script clears the injected ImGui mouse pin

## Context

`Projects/BrokenEngineSandbox/Documents/AgentHarness.md:193` tells the reader that a `hover`, `click`,
or `mouse move` "pins an ImGui mouse position that persists across scripts". Taken at face value, that
sentence says the pin survives subsequent commands, so an agent can move the mouse, advance frames with
`key`, and then send a coordinate-free `mouse wheel` at the pinned pixel. That does not work, and the
failure is silent: after a `key` command `describe_ui.mouse` reports ImGui's no-mouse sentinel
(`-FLT_MAX`) and the wheel lands nowhere near the intended widget. During this session's wheel-zoom
verification that produced a false negative that was only caught by re-testing.

Confirmed root cause, from current source:

- `AgentInput::BeginScript` clears `mbImGuiMousePosPinned` for every new script
  (`Engine/Source/Agent/AgentInput.cpp:64-71`); its comment states the intent precisely — the pin is
  cleared at the *next script's* start, not at the previous script's `Finish`, so it survives only up to
  the next command.
- The `kKey` script never re-pins: it only sets and clears a synthetic VK
  (`AgentInput.cpp:329-356`). Only a coordinate-carrying script re-pins, via `IssueImGuiMousePos`
  (`AgentInput.cpp:94-104`, called from the label-addressed paths at `:138`, `:202`, `:241`, `:268` and
  from `kMouse` with coordinates at `:366`).
- With no pin, `ImGuiManager::Prepare` parks `io.MousePos` at the no-mouse sentinel
  (`Engine/Source/Graphics/Managers/ImGuiManager.cpp:489-493`).

`Engine/Source/Agent/AGENTS.md` already states the behavior correctly ("It remains pinned for post-script
UI inspection until the next script begins"), so the code is behaving as designed and only the harness
document is wrong. Authority order therefore resolves to a documentation fix, not a behavior change.

## Design

Correct the `AgentHarness.md` paragraph so it states the actual lifetime: an injected mouse position is
pinned from the moment a coordinate-carrying script issues it until the *next* script starts, and any
later script that carries no coordinate — `key` in particular — clears it, after which `describe_ui.mouse`
reverts to the no-mouse sentinel and a coordinate-free `mouse wheel` no longer targets the previous
pixel. State the resulting rule for agents directly: send the wheel with explicit coordinates, or re-issue
`mouse move` immediately before it, rather than relying on a pin surviving an intervening `key`.

Documentation only; no engine or harness behavior changes.

## Critical files

- `Projects/BrokenEngineSandbox/Documents/AgentHarness.md` — the "Injected mouse position owns
  `io.MousePos`" paragraph (`:193`), plus the `mouse` command entry (`:179`) only if that entry needs the
  cross-reference to stay coherent.
- `Engine/Source/Agent/AgentInput.cpp` — read-only behavior evidence (`:64-71`, `:94-104`, `:329-356`).
- `Engine/Source/Agent/AGENTS.md` — read-only; already correct and must stay consistent with the new
  wording.

## In scope

- The wording of the injected-mouse-position paragraph in `Projects/BrokenEngineSandbox/Documents/AgentHarness.md`,
  and the minimum adjacent wording needed for it to read coherently.

## Out of scope

- Every code change, including making the pin survive a `key` script or re-pinning inside `kKey`
- `AgentInput` script phases, `AgentUiRegistry`, command schema, and JSON fields
- `Engine/Source/Agent/AGENTS.md`, which already describes the behavior correctly
- Any other section of `AgentHarness.md`

## Risk tier and invariants

Change Workflow Tier 1 — documentation only, no public signature or invariant exposure. Invariant to
preserve: the document must not contradict `Engine/Source/Agent/AGENTS.md` or current `AgentInput`
behavior.

## Acceptance criteria

- The paragraph states the pin's real lifetime and names `key` as a clearing command, and a reader
  following it would not expect a coordinate-free wheel after an intervening `key` to hit the pinned
  pixel.
- No source file changes; the statement matches `AgentInput.cpp:64-71` and `:329-356`.
