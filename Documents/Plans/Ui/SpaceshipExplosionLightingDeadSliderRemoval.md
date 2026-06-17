# Spaceship Explosion Lighting — Dead Tweak Slider Removal

## Context

Surfaced during execution of `Engine/DeadCodeAndUnusedIncludesSweep.md` (item 2, Explosions write-only
fields). That sweep removed the write-only Explosions SOA field `pfLightPercents` and its fully-orphaned
spawn parameter `SpawnInfo::fLightPercent`, deleting the four game-side `.fLightPercent = …` initializer
writes. One of those (Spaceships) read the Tweaks slider wrapper
`game::gSpaceshipExplosionLightingIntensity` via `.Get()` — its **only** functional consumer. Because
`pfLightPercents` was write-only (never read to drive anything), this slider was already non-functional;
removing the dead field made it a fully dead UI control.

The wrapper is still:
- declared at `LightingWrappers.h:79`,
- defined at `LightingWrappers.cpp:54` (`engine::Wrapper gSpaceshipExplosionLightingIntensity(0.0f, 0.0f, 2.0f)` — default 0.0, so it does nothing even at the slider extreme),
- registered as a live Tweaks slider "Spaceship Explosion Intensity" at `TweaksScreenLightingEffectsLighting.cpp:50`.

It now has zero readers in game logic (grep-verified at sweep time).

## Design

1. Re-grep to confirm `gSpaceshipExplosionLightingIntensity` still has zero functional readers (only the
   declaration / definition / Tweaks registration remain).
2. Remove the three sites: the `extern` (`LightingWrappers.h:79`), the definition
   (`LightingWrappers.cpp:54`), and the Tweaks slider row (`TweaksScreenLightingEffectsLighting.cpp:50`).
   Keep the surrounding "Spaceships - Explosion" section comment only if other wrappers remain under it
   (it currently holds just this one — drop the now-empty section header).
3. **Persistence check (the one open question):** determine whether `LightingWrappers` sliders are
   serialized into a persisted tweaks file (the game persists "tweaks settings ... POD structs with an
   embedded `kiVersion`" per `Projects/BrokenEngineSandbox/Source/CLAUDE.md`). If the wrapper participates
   in a persisted, index- or layout-sensitive tweaks blob, bump the relevant `kiVersion` so stale saved
   tweaks reject/migrate cleanly; if tweaks persistence is keyed by name (or these wrappers are not
   persisted), no version change is needed. Resolve at grill.

## Out of scope

- The other `LightingWrappers` (Missiles exhaust, Enemy blaster, etc.) — all still have live readers.
- The Explosions dead-field removal itself (landed in the sweep).
- Any change to the explosion visual/lighting behavior — the slider already drove nothing.

## Acceptance criteria

- `gSpaceshipExplosionLightingIntensity` fully removed (declaration, definition, Tweaks row); grep-clean.
- Client builds clean; the Tweaks Lighting Effects → Lighting tab no longer shows a "Spaceship Explosion
  Intensity" slider.
- If the wrapper was persisted, the tweaks-settings version was bumped; otherwise no version change.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Ui/LightingWrappers.h` (`:79` extern)
- `Projects/BrokenEngineSandbox/Source/Ui/LightingWrappers.cpp` (`:54` definition)
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/TweaksScreen/TweaksScreenLightingEffectsLighting.cpp`
  (`:50` slider registration)
- Tweaks-settings persistence path (verify whether this wrapper is serialized + its `kiVersion`).

## Notes

- Client-only (the wrapper is client-only by vcxproj inclusion per `Ui/CLAUDE.md`). No
  CRC/determinism/network exposure.
- One open decision (persistence/version impact) — resolve via `/external-grill-plan`.
