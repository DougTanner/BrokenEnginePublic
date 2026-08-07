<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-07T20:13:45.809Z","dependsOn":[]} -->
# Floor the ImGui style scale so short framebuffers cannot truncate style sizes to zero

## Context

A Debug client launched (or live-resized) to a framebuffer height below 270 px trips
`IM_ASSERT(g.Style.WindowBorderHoverPadding > 0.0f && "Invalid style setting!")`
(`ThirdParty/imgui/imgui.cpp:10853`) on the next `ImGui::NewFrame`, including the dummy
frame cycle the manager runs during construction (`Engine/Source/Graphics/Managers/ImGuiManager.cpp:162`).

Confirmed root cause, from current source:

- `ImGuiManager` derives `mfUiScale = framebufferHeight / kfUiReferenceHeight` with
  `kfUiReferenceHeight = 2160.0f` (`ImGuiManager.h:112`), at construction
  (`ImGuiManager.cpp:152`) and again on a framebuffer-extent change (`ImGuiManager.cpp:462`).
  Both guard only against a zero height, not a small one.
- `SetupThemeGeometry` scales the whole style with `rStyle.ScaleAllSizes(2.0f * fUiScale)`
  (`ImGuiManager.cpp:223`).
- `ImGuiStyle::ScaleAllSizes` truncates fields to integers, including
  `WindowBorderHoverPadding = ImTrunc(WindowBorderHoverPadding * scale_factor)` from a default of
  `4.0f` (`ThirdParty/imgui/imgui.cpp:1531`).

So the effective factor is `2.0f * height / 2160.0f`, and `ImTrunc(4.0f * factor)` reaches `0.0f`
whenever `height < 270`, which the assert then rejects. `WindowMinSize` has the sibling assert one
line earlier (`imgui.cpp:10852`) and is scaled the same way.

Reproduced twice with the Debug client during an unrelated session: launching `--windowed 1600x200`,
and live-resizing to 1594x184. `1600x900` and fullscreen `2560x1440` are clean. During the same
session a client resized to 1600x184 terminated with "exited without callstack" (harness client log
tail), consistent with the same assert firing.

Exposure is Debug-only (an `IM_ASSERT`); Release truncation still produces a zero-size style field
but does not abort. This is pre-existing behavior, unrelated to the session that observed it.

## Design

Clamp the geometry scale so truncation cannot reach zero, in `ImGuiManager` only — `ThirdParty/imgui`
is not modified.

Apply a lower bound to the factor passed to `ScaleAllSizes` inside `SetupThemeGeometry`, chosen so
every truncated field the asserts cover stays at or above one pixel: the binding field is
`WindowBorderHoverPadding` (default `4.0f`), so a factor of at least `0.25f` keeps it at `1.0f`.
`WindowMinSize` (default `32.0f`) is satisfied by the same bound. Floor only the style factor;
`FontScaleDpi` and `mfUiScale` itself keep their computed value, so the font and every
`engine::UiScale()`-based layout continue to shrink with the framebuffer as they do today.

Keep the existing zero-height guards as they are; this bound subsumes the small-height case without
changing behavior at any height at or above 270 px, where the factor already exceeds the bound.

## Critical files

- `Engine/Source/Graphics/Managers/ImGuiManager.cpp` — `SetupThemeGeometry` (`:215-224`) and its two
  callers, construction (`:148-155`) and the per-frame extent-change block (`:455-470`).
- `Engine/Source/Graphics/Managers/ImGuiManager.h` — `kfUiReferenceHeight` (`:112`) and the place a
  named minimum-factor constant belongs, if one is added.
- `ThirdParty/imgui/imgui.cpp` — read-only evidence at `ImGuiStyle::ScaleAllSizes` (`:1525-1540`) and
  the `NewFrame` asserts (`:10852-10853`); vendored ImGui is not modified.

## In scope

- The style-geometry scale factor used by `ImGuiManager::SetupThemeGeometry`, and a named constant
  for its lower bound if one is introduced.

## Out of scope

- `mfUiScale` itself, `FontScaleDpi`, `engine::UiScale()`, and every screen layout that consumes them
- Theme colors, `ApplyThemeColors`, the theme palettes, and the UI Opacity / Opaque UI settings
- Any `ThirdParty/imgui` or `ThirdParty/implot` edit
- Window/swapchain resize handling, minimize behavior, and the existing zero-height guards
- The wheel-ownership gate in `Projects/BrokenEngineSandbox/Source/Input/Input.cpp`

## Risk tier and invariants

Change Workflow Tier 2 — scoped client-only UI geometry behavior in one subsystem. No determinism,
CRC, wire, serialization, save/replay, threading, or trust-boundary surface is touched; the change is
compiled into the client only. Invariant to preserve: at every framebuffer height at or above 270 px
the computed style is byte-for-byte what it is today.

## Acceptance criteria

1. A Debug client launched with `--windowed 1600x200` reaches "Enter main loop" and renders a frame
   with no ImGui assert and no early exit.
2. A Debug client at `--windowed 1600x900` is unchanged: it starts, renders, and its UI geometry
   matches current behavior.
3. Debug x64 client compiles and links.

## Verification

Use `/agent-harness` for both live launches (log check for "Enter main loop", one screenshot each) and
`/compile` for the Debug x64 client build.

## Notes

- The 270 px threshold is `4.0f * 2.0f * height / 2160.0f >= 1.0f`; it is derived, not measured, and
  the two reproductions (200 px and 184 px heights) sit below it while the clean runs sit above it.
