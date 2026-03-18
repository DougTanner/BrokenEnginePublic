# Architecture: ProcessMenuInput Split

Source: /external-architecture-review on Projects/BrokenEngineSandbox/Source

## Changes

### Projects/BrokenEngineSandbox/Source/Game.cpp
- Split `ProcessMenuInput()` (lines 499-626, 127 lines) into focused handler functions. The function currently handles 8+ concerns with nested `if constexpr` blocks. Extract:
  - Quit/pause logic (lines 501-531) — keep inline, only ~30 lines
  - Debug input block (lines 547-614) into a private `ProcessDebugInput(const MenuInput&)` method — this is the largest sub-block at ~67 lines with time scaling, connection, and graphics submenu handling
  - This reduces `ProcessMenuInput()` from 127 lines to ~60 lines [~30m]

## Verification Notes
- Verified: `ProcessMenuInput()` is 128 lines (499-626). The `if constexpr (kbEnableDebugInput)` block (lines 547-614) is self-contained and a natural extraction point
