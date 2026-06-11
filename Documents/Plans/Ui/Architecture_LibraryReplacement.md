# Architecture: Library Replacement — CurveWidget on ImPlot

## Context

Source: /external-architecture-review on `Engine/Source/Ui` (non-recursive), ThirdParty-replacement pass.
The directory's only library-shaped cluster is the hand-rolled ImGui curve editor. `CurveWidget.cpp`
(`:1-178`) reimplements what the already-vendored ImPlot provides: point hit-testing (`FindPointAt`),
pixel↔curve coordinate transforms (`PixelToCurve`), gridline drawing, and a 256-sample polyline render. The
rest of the directory was assessed and rejected: the `*WrappersBase` declaration tables, the `Wrapper`
contract, `NetworkUiControl`, and `HeightLerpWrapperQuartet` are engine-specific or below the triviality
threshold, and `CurveData`'s Fritsch-Carlson evaluation must stay in-house (load-bearing at runtime for
`LightingUniforms.cpp:47` shader-uniform baking; candidate libraries fail on dependency weight or
monotonicity guarantees).

## Design

### Engine/Source/Ui/CurveWidget.cpp / CurveWidget.h
- Rebuild `engine::CurveWidget(...)` on ImPlot — **coverage extension of an existing import, not a new
  library**: ImPlot is already in `/ThirdParty/implot` (license **MIT**, on the allow list; verified present
  including `ImPlot::DragPoint` in `ThirdParty/implot/implot.h`). Use `ImPlot::DragPoint` for control-point
  dragging, `PlotLine` for the 256-sample curve, `PlotInfLines` for the `shaders::kiMaxSpreadPasses` pass-tick
  overlay (`CurveWidget.cpp:88-97`), and ImPlot axes for gridlines/transforms. **Lines removable:** ~100-120
  of the 178-line file (net shrink modest — the widget becomes a ~60-80 line ImPlot-based function). Side benefit: ImPlot
  tracks per-ID drag state, removing the documented one-widget-at-a-time limitation of the shared
  function-local `static int siDragIndex` (`CurveWidget.cpp:116`; documented in `Engine/Source/Ui/CLAUDE.md`). [~1h]
- Engine-specific behaviors to re-express in ImPlot terms (the risk surface): add-point-on-click in empty
  space, right-click delete, endpoint X-locking, interior-point neighbor clamping (owned by `CurveData`,
  unchanged), and the "returns true while interacting" contract consumed by
  `Ui/Screens/TweaksScreen/TweaksScreenLighting.cpp:228`. [included above]

## Risks
- Rewrite, not drop-in deletion — interaction behaviors must be re-verified by hand in the Tweaks Lighting
  tab (single call site, low integration cost; trivially revertible).
- No new dependencies, no build-system changes, no transitive deps (ImPlot already compiled in).
- Confidence: MEDIUM (clean API fit, moderate LOC payoff; main value is battle-tested interaction handling
  + multi-instance drag).

## Critical files
- `Engine/Source/Ui/CurveWidget.cpp`, `Engine/Source/Ui/CurveWidget.h`
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenLighting.cpp` (call-site contract,
  likely unchanged)
- `Engine/Source/Ui/CLAUDE.md` (drop the single-instance drag-state caveat once fixed)

## Out of scope
- `CurveData.h` — evaluation math stays in-house (rejected: Boost.Math `pchip` drags the Boost tree for ~90
  lines; tinyspline (MIT) lacks monotonicity guarantees; tk::spline is GPL-2.0 — license hard-reject).
- Importing ImGuizmo/ImCurveEdit (MIT) — delegate-based sequencer component; the adapter would offset the
  deleted lines, and importing a repo for a secondary component is disproportionate when ImPlot is vendored.
- Any new `/ThirdParty/` import — none justified by this directory.

## Acceptance criteria
- Tweaks → Lighting curve editor reproduces: drag/add/delete points, endpoint X-lock, neighbor clamp,
  spread-pass tick overlay, live uniform updates while dragging (the `TweaksScreenLighting.cpp:228` contract).
- Two curve widgets interactable independently (the `siDragIndex` limitation gone). Note: the sole call site
  today renders one widget at a time (Old/New toggle), so verify with a temporary second widget instance.
- Client-only TU (`BT_CLIENT`-wrapped, client vcxproj only) — server build untouched.

## Notes
- Client-only debug-UI widget; no determinism/CRC, `kiVersion`, replay, or network exposure. Grill decision
  pre-staged: confirm the ImPlot styling/interaction details are acceptable for the Tweaks workflow before
  spending the rewrite (the current widget works; this trades bespoke math for maintained library code).

## Verification Notes (2026-06-10)
- License/import claims re-verified: `/ThirdParty/implot/` exists (`implot.h/.cpp`, `implot_items.cpp`,
  compiled inside the imgui unity unit per `ThirdParty/CLAUDE.md`, which lists "imgui + implot (MIT)"); MIT
  is on the License Policy allow list. This is a coverage extension of an existing import — no new library,
  no vcxproj changes.
- API existence confirmed in `ThirdParty/implot/implot.h`: `ImPlot::DragPoint` (`:1047`, returns bool with
  clicked/hovered/held out-params — fits the "returns true while interacting" contract), `PlotLine`
  (`:958-959`), `PlotInfLines` (`:1002`).
- Call-site contract confirmed: `TweaksScreenLighting.cpp:228` consumes `CurveWidget(...)`'s bool return to
  set the active-slider exclusive-render state; this is the only `CurveWidget` call repo-wide. The widget's
  documented contract (`CurveWidget.h:10-11`) matches. `siDragIndex` shared static at `CurveWidget.cpp:116`;
  spread-pass tick overlay at `:88-97` (`shaders::kiMaxSpreadPasses`); the one-widget-at-a-time caveat is in
  `Engine/Source/Ui/CLAUDE.md` as claimed.
- Fixes during verification: `TweaksScreenLighting.cpp` is engine-side
  (`Engine/Source/Ui/Screens/TweaksScreen/`), not under `Projects/` — Critical files corrected; file is 178
  lines, not 194 — LOC estimate corrected; acceptance criterion for multi-instance drag annotated (single
  call site today, needs a temporary second instance to verify).
- Out-of-scope rejections spot-checked: `CurveData` evaluation is load-bearing at
  `LightingUniforms.cpp:40-48` (per-frame uniform bake) — keeping Fritsch-Carlson in-house is correct;
  tk::spline GPL-2.0 is a hard reject per the License Policy.
