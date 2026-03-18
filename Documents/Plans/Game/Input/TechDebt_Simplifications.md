# Tech Debt: Input Code Simplifications

Source: /external-tech-debt on Projects/BrokenEngineSandbox/Source/Input

## Changes

### Projects/BrokenEngineSandbox/Source/Input/Input.cpp
- Lines 31-36: Replace function-scoped `static XMFLOAT2 sf2MousePosition` with a class member on `Input`. The class already tracks previous state via `mPreviousRawInputMenu`; the mouse position from that field (`mPreviousRawInputMenu.f2MousePosition`) can be used directly instead of maintaining a separate static [~15m]
- Line 32: Replace non-idiomatic `!::operator==(rRawInput.f2MousePosition, sf2MousePosition)` with standard comparison syntax. After switching to class member, this becomes `rRawInput.f2MousePosition != mPreviousRawInputMenu.f2MousePosition` (verify that `operator!=` is available for `XMFLOAT2`, otherwise use memcmp) [~5m]

## Verification Notes
- Line numbers verified correct. When replacing the static with `mPreviousRawInputMenu.f2MousePosition`, the explicit assignment at line 36 (`sf2MousePosition = rRawInput.f2MousePosition`) must also be removed since `mPreviousRawInputMenu` is already updated at line 85.
