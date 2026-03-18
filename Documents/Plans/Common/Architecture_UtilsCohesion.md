# Architecture: Utils.h Cohesion Improvement

Source: /external-architecture-review on Common/

Utils.h (445 lines) contains 10+ unrelated utility groups. This plan relocates misplaced functions
to more appropriate files, improving cohesion without creating new files.

## Changes

### Common/Utils.h
- Remove `ColorToVector` declaration (line 63) and related comment (lines 59-62) — move to MathUtils.h (already declared there at line 18)
- Remove `ColorToUint` declaration (lines 65-69) — move to MathUtils.h
- Remove `ColorLerp` declaration (lines 71-74) — move to MathUtils.h
- (GetStringValueFromHKLM — removed as dead code in TechDebt_DeadCode plan, no need to relocate)
- Keep `SizeInBytes` in Utils.h — used by both Engine AND DataPacker, Common is the correct shared location

### Common/Utils.cpp
- Move `ColorToVector` definition (lines 109-113) to MathUtils.cpp
- Move `ColorToUint` definition (lines 115-122) to MathUtils.cpp
- Move `ColorLerp` definition (lines 124-127) to MathUtils.cpp
- (GetStringValueFromHKLM and SizeInBytes — handled by other plans or staying in place)

### Common/MathUtils.h
- Add `ColorToUint` and `ColorLerp` declarations (ColorToVector already declared at line 18)

### Common/MathUtils.cpp
- Add `ColorToVector`, `ColorToUint`, `ColorLerp` implementations (moved from Utils.cpp)

## Verification Notes
- SizeInBytes must remain in Common/ because it is used by both Engine and DataPacker
- GetStringValueFromHKLM has zero callers and is handled as dead code removal instead
