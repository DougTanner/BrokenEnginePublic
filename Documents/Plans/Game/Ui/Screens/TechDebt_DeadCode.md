# Tech Debt: Screens Dead Code

Source: /external-tech-debt + /external-architecture-review on Projects/BrokenEngineSandbox/Source/Ui/Screens

## Changes

### Projects/BrokenEngineSandbox/Source/Ui/Screens/MenuUtils.h
- Line 28: Remove the `ToUtf8()` declaration — function is never called anywhere in the codebase [~5m]

### Projects/BrokenEngineSandbox/Source/Ui/Screens/MenuUtils.cpp
- Lines 6-36: Remove the `ToUtf8()` function definition — dead code, never called. All callers use `AppendUtf8()` instead [~5m]

## Verification Notes
- Confirmed zero callers via codebase-wide grep. Removing this also eliminates the UTF-8 encoding duplication between ToUtf8 and AppendUtf8 (previously noted in TechDebt_Duplication.md).
