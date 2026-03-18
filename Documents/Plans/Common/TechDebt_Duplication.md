# Tech Debt: Code Duplication Fixes

Source: /external-tech-debt on Common/

## Changes

### Common/Random.h + Common/Random.cpp
- Extract the xorshift64 step (3 lines: `x ^= x << 13; x ^= x >> 7; x ^= x << 17; rRandomEngine.uiState = x;`) into a single `inline uint64_t XorshiftNext(RandomEngine& rRandomEngine)` function
- Replace the 3 duplicate copies:
  - Random.h:26-30 (template Random<>)
  - Random.cpp:36-40 (Random(uint32_t))
  - Random.cpp:46-50 (Random(float))

### Common/Utils.h + Common/MathUtils.h
- Remove duplicate `ColorToVector` declaration from Utils.h:63 — keep it in MathUtils.h where color math logically belongs
- Note: The Architecture_UtilsCohesion plan moves the full Color function implementations to MathUtils, so this dedup aligns with that direction

### Common/Smoothed.h + Common/Smoothed.cpp
- Replace magic number 1024 in `InTheLastSecond` with `static constexpr int64_t kiCapacity = 1024`
- Update all 5 references in Smoothed.h:15 and Smoothed.cpp:9,11,14,28

## Verification Notes
- Xorshift helper must be `inline` in the header for hot-path performance (template Random<> is inlined)
- Smoothed.cpp magic number refs are at lines 9, 11, 14, 28 (not 16 as originally stated)
- ColorToVector dedup direction aligned with Architecture_UtilsCohesion plan (move TO MathUtils)
