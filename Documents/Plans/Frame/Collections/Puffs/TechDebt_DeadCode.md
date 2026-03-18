# Tech Debt: Dead Code

Source: /external-deep-analysis on Engine/Source/Frame/Collections/Puffs

## Changes

### Engine/Source/Frame/Collections/Puffs/Puffs.cpp
- Remove defensive `kuiInvalidControllerType` guard and comment (lines 51-55). Puffs only has `AddControlled()` — no uncontrolled puffs can exist, making this guard dead code. Other Controller collections (e.g., PointLights) legitimately need this guard because they support both `Add()` and `AddControlled()`, but Puffs does not. [~5m]

## Verification Notes
- Confirmed: `PuffsPostRender` declares only `AddControlled()`, no `Add()` method exists
- Confirmed: PointLights has both `Add()` and `AddControlled()`, validating that the guard is needed there but not here
- The second `continue` for `!bDestroysSelf` (lines 60-63) should be kept — a controlled puff with `bDestroysSelf = false` is a valid configuration
