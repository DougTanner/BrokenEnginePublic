# Tech Debt: SunAngle Conditional Duplication

Source: /external-tech-debt on Projects/BrokenEngineSandbox/Source/Graphics

## Changes

### Projects/BrokenEngineSandbox/Source/Graphics/Camera.cpp
- Lines 134-147: The `if constexpr` branches in `SunAngle()` duplicate the return statement and overall logic structure, differing only by the `mbShowImGui` condition. Combine into a single block: compute `bUseOverride` conditionally, then return once [~5m]
  ```cpp
  bool bUseOverride = (game::gpGame->meUiState == game::UiState::kGraphics);
  if constexpr (kbEnableDebugInput)
  {
      bUseOverride = bUseOverride || game::gpGame->mbShowImGui;
  }
  if (bUseOverride)
  {
      return engine::gSunAngleOverride.Get();
  }
  ```

## Verification Notes
- Line numbers verified correct. Ensure `if constexpr` remains around the `mbShowImGui` check so the debug-only symbol is not referenced in release builds.
