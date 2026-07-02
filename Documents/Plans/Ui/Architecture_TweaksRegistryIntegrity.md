# Architecture: Tweaks Slider Registry Integrity

## Context
Source: /external-architecture-review on Engine/Source (recursive). The flat `TweaksSliderMap` keyspace silently drops duplicate keys (`insert` at `TweaksSliderMap.cpp:22`), and three live collisions exist today — two sections alias one wrapper, the loser is UI-unreachable, and which side wins depends on cross-TU static-init order. The first-open drift audit (`TweaksScreenBase.cpp:375-440`) is structurally blind to this class (key present + lookup succeeds → no orphan, no miss). Separately, one registrar entry has no rendered slider, and two `TweakSection` enumerators exist only for game consumers.

## Design

### Engine/Source/Ui/Screens/TweaksScreen/TweaksSliderMap.cpp
- Add a duplicate-key ASSERT (or kError log) on insert in `TweaksSliderMapRegistrar` — `insert`'s bool result at line 22 is currently discarded; static-init-time check, zero runtime cost [~5m]

### Colliding key renames (rename registrar key + every `WrapperSlider` call site naming it)
- `"Texel Ramp Speed"`: `TweaksScreenShadow.cpp:17` (`gShadowTexelRampMetersPerSec`) collides with `TweaksScreenLighting.cpp:59` (`gLightingTexelRampMetersPerSec`) — rename the Shadow side (e.g. `"Shadow Texel Ramp Speed"`), update slider sites `TweaksScreenShadow.cpp:49` and `TweaksScreenLighting.cpp:188` as needed [~10m]
- `"Temporal Blend"`: `TweaksScreenShadow.cpp:18` vs `TweaksScreenLighting.cpp:60` — same treatment, slider sites `TweaksScreenShadow.cpp:50` / `TweaksScreenLighting.cpp:189` [~10m]
- `"Grow"`: `TweaksScreenShadow.cpp:38` (`gObjectShadowsGrow`) vs game `TweaksScreenHexShield.cpp:15` (`gHexShieldGrow`) — rename one side; slider sites `TweaksScreenShadow.cpp:72` / game `TweaksScreenHexShield.cpp:36` [~10m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenPbr.cpp
- `"Day Brightness"` is registered (`TweaksScreenPbr.cpp:17`, wrapper `gPbrDayBrightness`, live consumer `LightingUniforms.cpp:256`) but never rendered — the only true orphan repo-wide; the audit warns every debug session and the uniform is untunable. Add `WrapperSlider("Day Brightness", kiSection, 1.0f);` in the Engine Variables group (~line 59), or delete the registrar entry if code-tuned-only is intended [~5m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.h — layer integrity
- `TweakSection::kHexShield` (:18) and `::kParticles` (:21), plus mirrored `TweakSectionFlags` (:38,:41), have game-only consumers (`TweaksScreenHexShield.cpp:33`, `TweaksScreenParticles.cpp:64`) — the enum doubles as the section-registry index, but unlike `engine::PacketType`'s `kGamePacketStart` there is no opaque extension point; game tabs must be named in the engine enum. Either add a game-extensible section mechanism (game-supplied section count/name table, `kGameSectionStart` analog) or accept + document the extension-registry rationale at the enum [~30m if extension point; ~5m if accept]

## Critical files
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksSliderMap.cpp`, `TweaksScreenBase.h`, `TweaksScreenShadow.cpp`, `TweaksScreenLighting.cpp`, `TweaksScreenPbr.cpp`
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/TweaksScreen/TweaksScreenHexShield.cpp`

## Out of scope
- Per-section key prefixing across the whole registrar (the Water registrar's generic keys — `"Intensity"`, `"Add"`, `"Angle"`, `"Speed"` — keep this bug class live long-term; a broader naming scheme is a follow-up decision, not this plan)
- The subtab apply-flag handshake dedup (`Ui/Refactor_TweaksScreenQuickWins.md`)
- Slider values/tuning themselves

## Notes
- Invariant exposure: none — client-only debug UI; no determinism/CRC/wire/save exposure. Renamed keys change which saved tweak entries match if tweak keys are persisted anywhere — verify `TweaksSliderMap` persistence before renaming (audit suggests keys are registration-time only)
- Grill decisions: (a) "Day Brightness" — add slider vs delete entry (recommend add); (b) `TweakSection` game enumerators — extension point vs accept+document (recommend accept+document; the enum-as-registry-index pattern is load-bearing and small)
