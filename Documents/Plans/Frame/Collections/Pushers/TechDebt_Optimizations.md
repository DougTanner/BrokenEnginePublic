# Tech Debt: Optimizations

Source: /external-tech-debt on Engine/Source/Frame/Collections/Pushers

## Changes

### Engine/Source/Frame/Collections/Pushers/PushersUpdate.cpp
- Line 11: Change `gpppuiPusherZones` type from `int64_t` to `int16_t` — indices store collection element indices which are well under 32K. Cuts zone memory from ~10MB to ~2.5MB per thread [~10m]
- Line 104: Add `static_cast<int16_t>(i)` for narrowing assignment into zone array [~1m]
- Line 122: Update `int64_t* piZone` local to `int16_t*` to match new type [~1m]
- Line 139: The `int64_t i = piZone[j]` assignment will implicitly widen from int16_t — no change needed but verify compiles clean [~0m]
- Lines 21-36: Replace per-element copy loop in `PushersInterpolate::Update()` with 5 `memcpy` calls (one per SOA array: pVecPositions, pfRadii, pfIntensities, pfPowers, pFlags). All fields are straight copies with no transformation [~5m]

### Engine/Source/Frame/Collections/Pushers/Pushers.h
- Lines 64-66: Wrap `BeginRender`, `Render`, `EndRender` declarations in `#if defined(BT_CLIENT)` / `#endif` — matches Explosions pattern [~2m]

### Engine/Source/Frame/Collections/Pushers/Pushers.cpp
- Line 3: Wrap `#include "Profile/ProfileManager.h"` in `#if defined(BT_CLIENT)` [~1m]
- Lines 75-90: Wrap `siTotalCount`, `BeginRender`, `Render`, `EndRender` implementations in `#if defined(BT_CLIENT)` / `#endif` — matches Explosions pattern [~2m]

## Verification Notes

- All file paths and line numbers verified against source
- `#ifdef BT_CLIENT` render guard pattern confirmed consistent with Explosions (the other always-compiled collection)
- memcpy confirmed safe: all SOA arrays are trivially copyable types, Update() performs pure copy with no transformation
- Zone index narrowing: int16_t supports up to 32,767 indices; if pusher counts could exceed this, use uint16_t (65,535) instead
