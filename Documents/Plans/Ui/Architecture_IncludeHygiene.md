# Architecture: Include Hygiene (Engine/Source/Ui)

## Context

Source: /external-architecture-review on `Engine/Source/Ui` (non-recursive). The directory is a near-perfect
leaf: 33 include directives total, zero direct std/third-party includes, zero cycles, zero dependencies on the
rest of `Engine/Source`. Three residual hygiene items: one unused include that silently drags `CurveData.h`
into the game PCH (defeating the documented "wrapper edits don't recompile the world" isolation for that
header), the game PCH's load-bearing include order being uncommented, and a handful of `std::` math/stdio
symbols that compile only via MSVC-STL-internal transitive includes rather than an explicit
`ExternalHeaders.h` anchor.

## Design

### Engine/Source/Ui/WrapperBase.h
- Remove `#include "CurveData.h"` (`:4`, inside the `BT_CLIENT` guard at `:3-5`) — no symbol from
  `CurveData.h` (`CurveData`, `ImVec2`) is referenced anywhere in `WrapperBase.h` (the `Wrapper` class
  `:10-209` and the extern globals `:212-230` are float/vector-only). Every real `CurveData` consumer
  includes it via its own path: `LightingWrappersBase.h:7`, `CurveWidget.h:5`, and
  `Ui/Screens/TweaksScreen/TweaksScreenLighting.cpp` (via `CurveWidget.h` and `LightingWrappersBase.h`).
  Side benefit: `WrapperBase.h` reaches the game PCH via `Pch.h:94-95` → game wrapper headers → this file,
  so the removal takes `CurveData.h` (and its `ImVec2` surface) out of the PCH closure. [~5m]

### Projects/BrokenEngineSandbox/Source/Pch.h
- Comment the load-bearing include order of the `:92-97` block: `Shaders/ShaderLayouts.h` (`:93`) must
  precede `Engine.h` (`:97`) because engine TUs (`Ui/WrapperBase.cpp:25` `shaders::kiMaxDebugTextures`,
  `Ui/CurveWidget.cpp:88/91/93` and `Ui/LightingWrappersBase.cpp:17` `shaders::kiMaxSpreadPasses`, plus the
  `Graphics/Render` uniform builders) consume `shaders::` constants from
  `Engine/Data/Shaders/ShaderLayoutsBase.h` exclusively via the PCH — there is no engine-side include of
  that header. The PCH-supplied convention is engine-wide and stays; only the ordering constraint gets
  documented (mirrors `Engine.h`'s own "include order is load-bearing" comments). [~5m]

### Common/ExternalHeaders.h
- Anchor `<cmath>` and `<cstdio>` in the standard-headers block (`:59-102`) — Ui files use `std::round`
  (`WrapperBase.h:198`), `std::sqrt` (`CurveData.h:167`), `std::nextafter` (`CurveData.h:58`), `std::lerp`
  (`HeightLerpWrapperQuartet.cpp:10`), `std::ceil`/`std::floor` (`CurveWidget.cpp:75-76`), and
  `std::snprintf` (`CurveWidget.cpp:57`), which currently resolve only through MSVC-STL-internal transitive
  includes (plus `corecrt_math.h` at `:107`). One-line additions at the fix site; the Ui files themselves
  are untouched (PCH-supplied symbols are the sanctioned convention). [~5m]

## Critical files
- `Engine/Source/Ui/WrapperBase.h`
- `Projects/BrokenEngineSandbox/Source/Pch.h` (comment only)
- `Common/ExternalHeaders.h`

## Out of scope
- Adding direct `Shaders/ShaderLayouts.h` includes to the three Ui TUs — would diverge from the engine-wide
  PCH convention (zero engine TUs include it directly; the game `Pch.h:93` is the sole include site).
- Consumer-side wrapper-include fixes in `Engine/Source/Graphics` — owned by
  `Graphics/Architecture_IncludeHygiene.md` (`CameraBase.cpp`, `Graphics.cpp`, …).
- The deliberate non-aggregation of wrapper headers into `Engine.h` (`Engine/Source/Ui/CLAUDE.md`: don't
  "fix" this).
- `WrapperBase.h`/`WrapperBase.cpp` symbol changes of any kind — `Frame/Architecture_BaseHeightWrapperDeterminism.md`
  and `Engine/WrapperGetIndexFailLoud.md` own their respective edits to these files.

## Acceptance criteria
- Client and server build clean after the `WrapperBase.h:4` removal; no TU anywhere relied on the transitive
  `CurveData.h` path (grep for `CurveData` consumers re-run at execution).

## Notes
- No determinism/CRC, `kiVersion`/`.pack`, replay, or guard-scope exposure — include/comment mechanics only,
  compile-checked. No grill decisions.

## Verification Notes (2026-06-10)
- `WrapperBase.h:4` include confirmed (inside the `BT_CLIENT` guard `:3-5`); read the full header — no
  `CurveData` or `ImVec2` reference anywhere in it (class `:10-209`, externs `:212-230`). Repo-wide
  `CurveData` grep: every consumer has its own include path — `LightingWrappersBase.h:7`, `CurveWidget.h:5`;
  the two TUs that *use* `CurveData` symbols (`TweaksScreenLighting.cpp` via `CurveWidget.h:4` +
  `LightingWrappersBase.h:5`; `LightingUniforms.cpp` via `LightingWrappersBase.h:6`) both compile clean
  without the transitive path. PCH-closure claim holds: `Pch.h:94-95` → `HexShieldWrappers.h:3` /
  `WindDepositsWrappers.h` → `Ui/WrapperBase.h`.
- `Pch.h:92-97` block confirmed uncommented; `Shaders/ShaderLayouts.h` at `:93`, `Engine.h` at `:97`.
  Repo-wide grep: the *only* include of `ShaderLayouts.h`/`ShaderLayoutsBase.h` in C++ source is `Pch.h:93`
  (plus the game `ShaderLayouts.h:5` including the base) — the "engine TUs consume `shaders::` exclusively
  via the PCH" claim holds (`WrapperBase.cpp:25` `kiMaxDebugTextures`; `CurveWidget.cpp:88/91/93`,
  `LightingWrappersBase.cpp:17` `kiMaxSpreadPasses`).
- `ExternalHeaders.h` standard block is exactly `:59-102`; neither `<cmath>` nor `<cstdio>` present
  (`corecrt_math.h` at `:107` is the only math-adjacent anchor). All cited `std::` usages re-verified at
  their lines: `std::round` `WrapperBase.h:198`, `std::sqrt` `CurveData.h:167`, `std::nextafter`
  `CurveData.h:58`, `std::lerp` `HeightLerpWrapperQuartet.cpp:10`, `std::ceil`/`std::floor`
  `CurveWidget.cpp:75-76`, `std::snprintf` `CurveWidget.cpp:57`.
- Duplication checks: no overlap with `Graphics/Architecture_IncludeHygiene.md` (consumer-side adds in
  Graphics TUs), `Frame/Architecture_BaseHeightWrapperDeterminism.md` (deletes `gBaseHeight` symbols —
  different lines in `WrapperBase.h`; co-schedule), or `Engine/WrapperGetIndexFailLoud.md` (`GetIndex`
  method body). Kept as written.
