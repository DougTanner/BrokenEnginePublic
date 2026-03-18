# Tech Debt: Dead Code

Source: /external-tech-debt on Engine/Source/Frame/Collections/SmokeTrails

## Changes

### Engine/Source/Frame/Collections/SmokeTrails/SmokeTrailsUpdate.cpp
- Move the two non-empty functions (Update:8-11, Sync:13-20) into SmokeTrails.cpp, then delete SmokeTrailsUpdate.cpp entirely [~10m]
  - Update() is a one-line comment stub
  - Sync() is 7 lines of real logic
  - The remaining stubs (Update:22-24, PreCollision:26-28, PostCollision:79-81, AreaDamage:83-85) are empty no-ops required by the framework — move them to SmokeTrails.cpp alongside the other stubs (Spawn, Transfer, Destroy)
- After moving, remove SmokeTrailsUpdate.cpp from the vcxproj filter [~2m]

### Engine/Source/Frame/Collections/SmokeTrails/CLAUDE.md
- Update file structure section to reflect two files instead of three [~2m]

## Verification Notes
- All empty stubs confirmed required by framework fold expressions in FrameUtils.h
- Merging SmokeTrailsUpdate.cpp into SmokeTrails.cpp results in ~120 lines, well under 500
- No file-scope statics or internal dependencies in SmokeTrailsUpdate.cpp; merge is safe
