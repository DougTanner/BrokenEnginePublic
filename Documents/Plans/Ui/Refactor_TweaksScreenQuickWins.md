# Refactor: TweaksScreen Quick Wins

## Context
Source: /external-refactor-clean on Engine/Source (recursive). The batch's one structural cleanup — the copy-paste-enforced subtab handshake — plus small string_view/consistency items. Allocation and guard/vcxproj sweeps came back clean; the CurveWidget/UpdateFocus/SectionFlag items are already owned by `Meta/ReviewSweepQuickWins.md`.

## Design

### Subtab apply-flag handshake helper
- Add `bool TweaksScreenBase::BeginSubtab(const char* pcLabel, int64_t iSection, int8_t iTab)` to `TweaksScreenBase.h` wrapping `ImGui::BeginTabItem` + the `(mApplySubtab & SectionFlag(kiSection)) && mActiveSubtab[kiSection] == N` SetSelected ternary + the clear/write-back branches; `EndTabItem` stays at call sites. Convert the 15 verbatim engine copies: `TweaksScreenWater.cpp:117-126, 187-196, 223-232, 253-262`; `TweaksScreenLighting.cpp:120-129, 195-204, 254-263, 318-327, 331-340`; `TweaksScreenWind.cpp:45-54, 93-102`; `TweaksScreenSmoke.cpp:55-64, 107-116`; `TweaksScreenSound.cpp:47-56, 68-77`. `Screens/TweaksScreen/AGENTS.md` currently *instructs* hand-replication ("must replicate this apply-flag handshake in every BeginTabItem") — update the rule to "use BeginSubtab". Game-side copies (3) can adopt in passing or later. Behavior-preserving; also mostly dissolves the >100-line flat renderers without per-tab splits [~1h]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.cpp
- `mActiveSlider = mapKey.data();` (:180) — assigning `.data()` to a `string_view` member re-scans via `strlen` and silently depends on NUL-termination the type doesn't guarantee (works only via the static-literal contract); `mActiveSlider = mapKey;` is exact and cheaper [~5m]
- Fix the stale "twice as wide as default" comment (:154) — predates the parameterized `fWidthMultiplier` [~5m]

### Sibling guard/include alignment
- `TweaksScreenBase.cpp:1-6` places non-corresponding includes *inside* the `#if defined(BT_CLIENT)` guard; all ten per-section siblings place them before it — align the ten siblings to guard-first (matches Base and the wider engine convention, e.g. `Graphics.cpp`/`ImGuiManager.cpp`; safer default for client-only headers like `TextureManager.h`, which is not self-guarded and sits outside the guard at `TweaksScreenWater.cpp:5`) [~15m]

## Critical files
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.{h,cpp}`, `TweaksScreenWater.cpp`, `TweaksScreenLighting.cpp`, `TweaksScreenWind.cpp`, `TweaksScreenSmoke.cpp`, `TweaksScreenSound.cpp` (+ the other five siblings for the guard alignment)
- `Engine/Source/Ui/Screens/TweaksScreen/AGENTS.md`

## Out of scope
- `TweakSection` game-extensibility (`Ui/Architecture_TweakSectionGameExtension.md` — shares `TweaksScreenBase.{h,cpp}`, co-schedule in one session)
- Wave-count radio list duplication of wrapper allowed-sets (accept — stable, one call site; would need a `Wrapper` allowed-values accessor)
- `HeightLerpWrapperQuartet` 4-key string assembly (~20 sites; note-tier)
- `CurveData.h` int/`operator[]` style items (code-style-review territory)

## Notes
- Invariant exposure: none — client-only debug UI; no determinism/CRC/wire. The handshake helper must reproduce the exact SetSelected/clear/write-back sequencing (tab restore across screen reopen depends on it) — convert one screen, verify tab behavior, then sweep the rest
- Grill decision: none — mechanical; the AGENTS.md rule update lands with the helper

## Verification Notes
- All 15 cited handshake copies verified verbatim at the cited line ranges (Water 4, Lighting 5, Wind 2, Smoke 2, Sound 2); game side has 3 more (Particles: Missile/Player/Spaceship). `mActiveSlider = mapKey.data();` confirmed at `TweaksScreenBase.cpp:180` (`mActiveSlider` is `std::string_view`, header :95); stale comment confirmed at :154. The AGENTS.md "must replicate this apply-flag handshake in every `BeginTabItem`" sentence confirmed.
- Dropped from the original draft: a `TweaksScreenWater.cpp:3-5` include-reorder sub-item — the current order (0/1/2 subdirectory depths ascending) already satisfies style rule 47's "fewer subdirectories first" reading; ambiguous at best and code-style-review territory regardless.
- The two dev-UI font-scale sites in `TweaksScreenBase::Render` (~:251 toggle bar, ~:358 per-section) already use the ImGui dynamic-font `PushFont(nullptr, GetStyle().FontSizeBase * kfUiScale)`/`PopFont()` pattern (migrated) — this plan's `BeginSubtab`/string_view/comment work doesn't touch them, but the added push/pop lines shifted nearby citations, so refresh line numbers at execution.
