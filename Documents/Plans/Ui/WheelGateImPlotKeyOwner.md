<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-07T20:13:48.541Z","dependsOn":[]} -->
# Extend the camera wheel gate to plots that claim the wheel through key ownership

## Context

The wheel-ownership gate added to `Projects/BrokenEngineSandbox/Source/Input/Input.cpp` (`:142-146`)
decides that the UI owns a wheel notch only when the hovered ImGui window can actually scroll, or when
ImGui's `WheelingWindow` lock still holds an earlier target. ImPlot does not route the wheel that way:
it claims the wheel with `ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, ...)`
(`ThirdParty/implot/implot.cpp:2041` and `:2058` for axis zoom, `:3123` and `:3646` for scrollable
legends), which the gate does not observe. A notch over such a plot inside a non-scrolling window
therefore both zooms the plot and zooms the camera.

Reachability, from current source:

- `Projects/BrokenEngineSandbox/Source/Profile/NetworkGraphs.cpp:26` builds plots whose X axis is not
  input-locked (`SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_AutoFit)`,
  `:28`), so the `x_hov[i] && !x_axis.IsInputLocked()` branch at `implot.cpp:2041` runs and takes
  ownership. Its host window is `ImGui::Begin("Network Graphs", nullptr, ImGuiWindowFlags_NoResize | ...)`
  (`NetworkGraphs.cpp:50`), which is not a scrolling window, so the existing gate leaves the notch with
  the camera as well.
- `Engine/Source/Ui/CurveWidget.cpp:34` is *not* reachable: it calls `SetupAxesLimits(..., ImPlotCond_Always)`
  (`:38`), which makes `ImPlotAxis::IsRangeLocked()` and hence `IsInputLocked()` true for both axes
  (`ThirdParty/implot/implot_internal.h:912-918`), so neither `SetKeyOwner` branch runs; and
  `ImPlotFlags_CanvasOnly` (`:33`) removes the legend, so the legend-scroll ownership sites cannot run
  either. It is listed here only so a future change that unlocks those axes is understood to reopen the
  case.

Exposure is debug UI only (the profiler's Network Graphs window). This edge sits immediately adjacent
to the wheel-gate change that introduced the rule but outside its approved boundary; the manager
deliberately deferred it.

## Design

Add key ownership as a third UI-owns-the-wheel term in the same expression in
`Input::Update`: treat the wheel as UI-owned when `ImGui::TestKeyOwner(ImGuiKey_MouseWheelY, ImGuiKeyOwner_NoOwner)`
is false, i.e. some widget owns the wheel key. Keep the existing hovered-scrollable-window and
`WheelingWindow` terms unchanged; this only widens the gate, never narrows it.

The ownership read has the same one-frame lag the existing terms already accept and document: the poll
runs before `NewFrame`, so it reads the owner ImPlot installed while rendering the previous frame
(`SetKeyOwner` writes both `OwnerCurr` and `OwnerNext`, `ThirdParty/imgui/imgui.cpp:10606`, and
`NewFrame` promotes `OwnerNext` into `OwnerCurr`, `:10129`). A consequence to keep in the comment:
ownership therefore persists for one poll after the cursor leaves the plot, so the first notch after
leaving a plot is suppressed for the camera — the same conservative direction as the existing
`WheelingWindow` term.

`TestKeyOwner` and `ImGuiKeyOwner_NoOwner` live in `ThirdParty/imgui/imgui_internal.h`
(`:1563`, `:3404`), which this file's existing context-pointer access already depends on. Guard the new
term with the same null-context check the current expression uses.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Input/Input.cpp` — the `bUserInterfaceOwnsScroll` expression and
  its explanatory comment (`:135-147`).
- `Projects/BrokenEngineSandbox/Source/Input/AGENTS.md` — the wheel-routing rule, updated only if the
  documented rule no longer matches.
- `ThirdParty/implot/implot.cpp`, `ThirdParty/implot/implot_internal.h`, `ThirdParty/imgui/imgui.cpp`,
  `ThirdParty/imgui/imgui_internal.h` — read-only evidence; vendored sources are not modified.

## In scope

- The `bUserInterfaceOwnsScroll` term and comment in `Input::Update`
- The matching sentence in `Projects/BrokenEngineSandbox/Source/Input/AGENTS.md` if the rule's wording
  becomes inaccurate

## Out of scope

- Any `ThirdParty/implot` or `ThirdParty/imgui` edit, including changing how ImPlot claims the wheel
- Curve-widget or Network Graphs plot configuration, axis locking, and layout
- Camera zoom behavior, zoom rate, and the scroll accumulator semantics in `RawInput`
- Keyboard/mouse-button routing and every non-wheel input path
- The ImGui style-scale assert (`Documents/Plans/Graphics/ImGuiStyleScaleFloor.md`)

## Risk tier and invariants

Change Workflow Tier 2 — scoped client-only input-routing behavior in one subsystem. `iScrollDelta`
feeds camera zoom, which is client-only presentation; no simulation, CRC, wire, serialization, or
threading surface is touched. Invariant to preserve: a single notch is never both consumed by the UI
and applied to the camera, and a notch over empty background or a non-scrolling, non-plot panel still
reaches the camera.

## Acceptance criteria

1. A wheel notch over a plot in the Network Graphs window zooms only the plot; the camera altitude is
   unchanged.
2. A wheel notch over empty background, and over a non-scrolling menu panel with no plot, still zooms
   the camera exactly as it does today.
3. A wheel notch over a scrolling panel still scrolls only that panel.
4. Debug x64 client compiles and links.

## Verification

Use `/agent-harness`: pin the mouse over the target with `mouse move`, then send a coordinate-carrying
`mouse wheel`, and compare camera altitude from `describe_scene`/`describe_ui` before and after. Use
`/compile` for the Debug x64 client build.

## Notes

- The originally reported reachability through `Engine/Source/Ui/CurveWidget.cpp` was checked against
  current source and does not hold, for the locked-axis and no-legend reasons recorded above.
