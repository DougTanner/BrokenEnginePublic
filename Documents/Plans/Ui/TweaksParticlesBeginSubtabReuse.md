<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-07T13:42:20.252Z","dependsOn":[]} -->
# TweaksScreenParticles: Use BeginSubtab for Saved-Subtab Selection

## Context

`game::TweaksScreen::RenderParticlesSection` (`Projects/BrokenEngineSandbox/Source/Ui/Screens/TweaksScreen/TweaksScreenParticles.cpp:62-172`) re-implements the saved-subtab select/apply state machine inline three times — lines 68-77 (Missile, tab 0), 102-111 (Player, tab 1), 136-145 (Spaceship, tab 2): a ternary `ImGuiTabItemFlags_SetSelected` on `ImGui::BeginTabItem`, a conditional `mApplySubtab.Clear(engine::SectionFlag(iSection))`, and a conditional `mActiveSubtab[iSection] = N`.

`engine::TweaksScreenBase::BeginSubtab(pcLabel, iSection, iTab)` (`Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.cpp:148-165`, public at `TweaksScreenBase.h:94`; `game::TweaksScreen` derives publicly at `TweaksScreen.h:8`) implements the identical sequence and returns the identical `BeginTabItem` result. All five engine tabbed sections call it (`TweaksScreenLighting.cpp:123`, `TweaksScreenWater.cpp:131`, `TweaksScreenSound.cpp:47`, `TweaksScreenSmoke.cpp:55`, `TweaksScreenWind.cpp:45`); Particles is the sole tabbed section that bypasses it — an abandoned sibling pattern left behind by the earlier `BeginSubtab` Tweaks refactor (referenced as prior work in `Documents/Features/Agent/AgentTweaksUiAutomation.md`).

Verified behavior-identical by direct comparison of the helper body against the inline blocks: the helper caches the selection predicate the inline form recomputes, `BeginTabItem` receives no reference to either member, tab literals 0-2 convert exactly to `int8_t`, `EndTabItem` stays caller-owned, and the six-frame slider-map drift audit (`TweaksScreenBase.cpp:448`) observes identical selection and apply-flag behavior. Any future correction to the base subtab state machine currently misses these three separately maintained copies.

Origin: accepted, reviewer-CONFIRMED finding from an /external-deep-analysis run over this exact file (architecture Lens D cohesion finding plus refactor-clean repeated-local-logic finding, same root cause). Unmet criterion: single ownership of the persisted-subtab state machine per the engine TweaksScreen tabbed-section contract (`Engine/Source/Ui/Screens/TweaksScreen/AGENTS.md`, tabbed-section persistence) and root `AGENTS.md` reuse-existing-mechanisms.

## Design

Replace each of the three inline `ImGui::BeginTabItem` calls and their select/apply blocks with the existing base helper, keeping every row body and `ImGui::EndTabItem()` call unchanged:

- Line 68 block becomes `if (BeginSubtab("Missile", iSection, 0))`
- Line 102 block becomes `if (BeginSubtab("Player", iSection, 1))`
- Line 136 block becomes `if (BeginSubtab("Spaceship", iSection, 2))`

No other statement changes. Expected cyclomatic complexity of `RenderParticlesSection` drops from 20 to 5.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Ui/Screens/TweaksScreen/TweaksScreenParticles.cpp` — the only file that changes
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.cpp` / `.h` — read-only: `BeginSubtab` definition and declaration; do not modify

## In scope

- `TweaksScreen::RenderParticlesSection` in `TweaksScreenParticles.cpp`: the three `ImGui::BeginTabItem` conditions and their `mApplySubtab`/`mActiveSubtab` select/apply blocks (lines 68-77, 102-111, 136-145)

## Out of scope

- The `gParticlesRegistrar` table and all `WrapperSeparatorText`/`WrapperSlider` row bodies (deliberate mirrored per-tunable row pattern; stays parallel with sibling TweaksScreen files)
- Tweak names, ranges, defaults, ordering, on-screen geometry, and `Documents/UserInterfaceDesign.txt` rules
- Any change to `TweaksScreenBase`, other TweaksScreen files, or a generic tweak-row framework
- Vendored ImGui

## Risk tier and invariants

Change Workflow Tier 1 — local behavior-preserving work with no public signature or invariant exposure: client-only (`BT_CLIENT`) debug UI, no determinism/CRC, wire, serialization, threading, or trust-boundary surface. Preserved invariants: saved-subtab restore on load, the once-per-lifetime six-frame slider-map drift audit, engine TweaksScreen registration/order contracts, and the at-most-one-ImGui-table rule (no table involved).

## Acceptance criteria

- All three Particles tabs go through `BeginSubtab`; no direct `mApplySubtab`/`mActiveSubtab` access remains in `TweaksScreenParticles.cpp`
- Row bodies, tab labels, tab order, and `EndTabItem` calls are byte-wise unchanged
- Client build compiles clean
- Saved-tab restore and the slider-map drift audit behave unchanged (diff-decisive given the verified helper equivalence; no runtime scenario required beyond compile)

## Notes

Historical anchors: Effort 1 (Quick Win), Impact 2 (Modest — restores single ownership, removes ~24 duplicated lines), Risk 1 (Low — mechanical, compile-checked, easily reverted).
