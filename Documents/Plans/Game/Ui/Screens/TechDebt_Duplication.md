# Tech Debt: UI Screens Code Duplication

Source: /external-tech-debt on Projects/BrokenEngineSandbox/Source/Ui/Screens

## Changes

### Projects/BrokenEngineSandbox/Source/Ui/Screens/HudScreen.cpp
- Lines 91-140 (RenderShieldBar) vs 142-191 (RenderArmorBar): These two functions are structurally identical, differing only in: (1) field accessor (`pfShields` vs `pfArmors`), (2) color constant (`kuiShieldColor` vs `kuiArmorColor`), (3) half-width-per-point constant, (4) bar vertical offset sign (`-0.5f` vs `+0.5f`), (5) icon descriptor set. Consolidate into a single `RenderBar()` function parameterized by these 5 values [~15m]

## Verification Notes
- Line numbers verified correct. Original plan also included MenuUtils.cpp ToUtf8/AppendUtf8 duplication, but that is superseded by TechDebt_DeadCode.md (ToUtf8 is dead code — removing it eliminates the duplication).
