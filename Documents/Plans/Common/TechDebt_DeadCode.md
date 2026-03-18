# Tech Debt: Dead Code Removal

Source: /external-tech-debt on Common/

## Changes

### Common/Random.h
- Remove `UniformRandom` floating-point template (lines 36-41) — unused anywhere in codebase
- Remove `UniformRandom` integral template (lines 43-48) — unused anywhere in codebase

### Common/Utils.h
- Remove `WaitAll` declaration (line 239) — unused outside Common/
- Remove `FromFloat` declaration (line 284) — completely unused in the codebase (cmft uses its own `cmft::halfFromFloat`, not this)
- Remove `GetStringValueFromHKLM` declaration (lines 288-289) — zero callers in entire codebase
- Remove `ToU32string` declaration (line 191) — zero callers in entire codebase

### Common/Utils.cpp
- Remove `WaitAll` definition (lines 156-162) — unused outside Common/
- Remove `FromFloat` definition (lines 164-168) — completely unused
- Remove `GetStringValueFromHKLM` definition (lines 67-107) — zero callers
- Remove `ToU32string` definition (lines 151-154) — zero callers

## Verification Notes
- All items verified: grep across full codebase confirms zero callers for each function
- FromFloat was incorrectly attributed to cmft usage in initial analysis — it is simply unused
- GetStringValueFromHKLM was initially in Architecture_UtilsCohesion plan as a relocation — changed to deletion since it has no callers
