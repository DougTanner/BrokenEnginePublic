# Refactor: TweaksScreen Quick Wins

## Context
Source: /external-refactor-clean on Engine/Source (recursive). The batch's one structural cleanup — the copy-paste-enforced subtab handshake — plus small string_view/consistency items. Allocation and guard/vcxproj sweeps came back clean; the CurveWidget/UpdateFocus/SectionFlag items are already owned by `Meta/ReviewSweepQuickWins.md`.

## Design

### Subtab apply-flag handshake helper
- Add `bool TweaksScreenBase::BeginSubtab(const char* pcLabel, int64_t iSection, int8_t iTab)` to `TweaksScreenBase.h` wrapping `ImGui::BeginTabItem` + the `(mApplySubtab & SectionFlag(kiSection)) && mActiveSubtab[kiSection] == N` SetSelected ternary + the clear/write-back branches; `EndTabItem` stays at call sites. Convert the 15 verbatim engine copies: `TweaksScreenWater.cpp:117-126, 187-196, 223-232, 253-262`; `TweaksScreenLighting.cpp:120-129, 195-204, 254-263, 318-327, 331-340`; `TweaksScreenWind.cpp:45-54, 93-102`; `TweaksScreenSmoke.cpp:55-64, 107-116`; `TweaksScreenSound.cpp:47-56, 68-77`. `Screens/TweaksScreen/CLAUDE.md` currently *instructs* hand-replication ("must replicate this apply-flag handshake in every BeginTabItem") — update the rule to "use BeginSubtab". Game-side copies (3) can adopt in passing or later. Behavior-preserving; also mostly dissolves the >100-line flat renderers without per-tab splits [~1h]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.cpp
- `mActiveSlider = mapKey.data();` (:180) — assigning `.data()` to a `string_view` member re-scans via `strlen` and silently depends on NUL-termination the type doesn't guarantee (works only via the static-literal contract); `mActiveSlider = mapKey;` is exact and cheaper [~5m]
- Fix the stale "twice as wide as default" comment (:154) — predates the parameterized `fWidthMultiplier` [~5m]

### Sibling guard/include alignment
- `TweaksScreenBase.cpp:1-6` places non-corresponding includes *inside* the `#if defined(BT_CLIENT)` guard; all ten per-section siblings place them before it — align the ten siblings to guard-first (matches Base; safer default for client-only headers like `TextureManager.h`, unguarded at `TweaksScreenWater.cpp:1-7`). Also fix `TweaksScreenWater.cpp:3-5` include ordering (style rule 47) [~15m]

## Critical files
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.{h,cpp}`, `TweaksScreenWater.cpp`, `TweaksScreenLighting.cpp`, `TweaksScreenWind.cpp`, `TweaksScreenSmoke.cpp`, `TweaksScreenSound.cpp` (+ the other five siblings for the guard alignment)
- `Engine/Source/Ui/Screens/TweaksScreen/CLAUDE.md`

## Out of scope
- Registrar key collisions, "Day Brightness" orphan, `TweakSection` enumerators (`Ui/Architecture_TweaksRegistryIntegrity.md` — same files, co-schedule in one session)
- Wave-count radio list duplication of wrapper allowed-sets (accept — stable, one call site; would need a `Wrapper` allowed-values accessor)
- `HeightLerpWrapperQuartet` 4-key string assembly (~20 sites; note-tier)
- `CurveData.h` int/`operator[]` style items (code-style-review territory)

## Notes
- Invariant exposure: none — client-only debug UI; no determinism/CRC/wire. The handshake helper must reproduce the exact SetSelected/clear/write-back sequencing (tab restore across screen reopen depends on it) — convert one screen, verify tab behavior, then sweep the rest
- Grill decision: none — mechanical; the CLAUDE.md rule update lands with the helper
