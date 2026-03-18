# Tech Debt: gbWindClear Bug

Source: /external-tech-debt on Engine/Source/Graphics/Render

## Changes

### Engine/Source/Graphics/Render/WindUniforms.cpp
- Fix `gbWindClear` flag handling (lines 20-26) — currently the flag is set `true` on toggle (line 23) but unconditionally reset to `false` on line 26, making the clear never take effect [~15m]
  - Move `gbWindClear = false;` (line 26) inside a conditional `if (gbWindClear)` block, so it only resets after the clear is handled
  - No `kPipelineWindClear` pipelines exist, so the clear cannot mirror smoke's dispatch-and-return pattern. Instead, the `if (gbWindClear)` block should reset the wind spread static state (`sf4PreviousWindArea`) and set `gbWindClear = false`, so the next frame starts fresh
  - `RenderTargetTextures.cpp:456` sets `gbWindClear = true` on texture recreation, which will correctly trigger the reset after this fix

## Verification Notes
- Bug confirmed: `gbWindClear = true` on line 23 is unconditionally overwritten to `false` on line 26, so the flag never persists to be checked
- `RenderTargetTextures.cpp:456` also sets `gbWindClear = true` on wind texture recreation, confirming the flag is intended to be used
- No `kPipelineWindClear` pipelines exist anywhere in the codebase -- the original plan's suggestion to mirror smoke's clear pipeline dispatch is not possible
- Fix should be conservative: guard the `false` assignment with `if (gbWindClear)` and reset `sf4PreviousWindArea` inside, then proceed with normal wind computation
