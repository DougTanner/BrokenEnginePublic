# Refactor: Per-Tab Registrars for `TweaksSliderMap`

## Context

The game-side `TweaksScreen` constructor in `Projects/BrokenEngineSandbox/Source/Ui/Screens/TweaksScreen/TweaksScreen.cpp` inserts ~175 string-literal entries into the global `engine::TweaksSliderMap` in one big initializer-list (lines ~13-191). Every per-tab `TweaksScreen<Tab>.cpp` file calls `WrapperSlider("Some Label", ...)` — those literal labels must stay byte-identical to the strings registered in this faraway constructor.

Effects of the current shape:
- Cross-file coupling: a label rename in `TweaksScreenWindDeposits.cpp` requires editing `TweaksScreen.cpp`.
- Single-point breakage: a typo in either file silently no-ops the slider (`WrapperSlider` returns on map-miss — see `TweaksScreenBase.cpp` line 92).
- `TweaksScreen.cpp` is dominated by registration boilerplate; `Render()` is 13 lines.

## Goal

Each `TweaksScreen<Tab>.cpp` registers its own keys at static-init time, co-locating registration with use-site so drift is impossible. Shrink `TweaksScreen.cpp` to its `Render()` glue.

## Approach

Add a small registrar helper to `engine::TweaksSliderMap`:

```cpp
// In TweaksSliderMap.h/.cpp (engine side)
struct TweaksSliderMapRegistrar
{
    TweaksSliderMapRegistrar(std::initializer_list<std::pair<std::string_view, Wrapper*>> entries);
};
```

Implementation calls `TweaksSliderMap::Get().insert(entries)` inside a `ScopedSuppressAllocationTracking` block. This preserves the existing allocation-tracking guarantee: STL hash buckets allocate, and the registrar runs at static-init time before main-loop tracking begins, but the suppression scope is kept for symmetry and any future reuse.

Each per-tab `.cpp` declares one no-op static:

```cpp
// In TweaksScreenWindDeposits.cpp (illustrative)
namespace
{
    const engine::TweaksSliderMapRegistrar gWindDepositsRegistrar
    {
        {"Player Deposit Width", &gWindDepositPlayerWidth},
        {"Player Deposit Intensity", &gWindDepositPlayerIntensity},
        // ...
    };
}
```

Each block becomes the source of truth for the strings used by that tab's `Render*` function. `TweaksScreen::TweaksScreen()` constructor body collapses to `= default`.

## File-by-file split

The existing entries map to per-tab files cleanly:

- HexShield (Edge / Wave / Direction sections, ~11 entries) → `TweaksScreenHexShield.cpp`
- Wind Deposits (Player / Spaceships / Player Blasters / Spaceships Blasters / Explosions, ~14 entries) → `TweaksScreenWindDeposits.cpp`
- Lighting Effects - Visible (Explosion Primary/Secondary visible, Crater visible, Player Impact visible, Hit Flash visible, Missile visible, Enemy Blaster visible, ~28 entries) → `TweaksScreenLightingEffectsVisible.cpp`
- Lighting Effects - Lighting (Explosion Primary/Secondary lighting, Crater lighting, Blaster Puff, Players lighting, Player Impact lighting, Hex Shield lighting, Missile lighting, Spaceship lighting, Hit Flash lighting, ~32 entries) → `TweaksScreenLightingEffectsLighting.cpp`
- Particles (Missile / Player / Spaceship per-explosion sliders, ~42 entries) → `TweaksScreenParticles.cpp`

Verify these allocations against the `gExplosion*` / `gCrater*` / `gPlayerImpact*` / `gHexShield*` Wrapper globals declared in `LightingWrappers.h` and `ParticleWrappers.h`. Some Visible/Lighting splits are by suffix (`...VisibleArea*` vs `...LightingArea*`); double-check each line stays in the file whose `Render*` function calls `WrapperSlider` on it.

## Constraints

- **Allocation tracking**: the slider map's `Get()` already wraps function-local-static construction in `ScopedSuppressAllocationTracking` because STL hash-table buckets allocate. The new `TweaksSliderMapRegistrar` constructor must also wrap its `insert()` in `ScopedSuppressAllocationTracking`. Static-init runs before allocation-tracking starts in practice, but landing the suppression keeps the contract explicit and tolerates future re-init.
- **Client/server**: every per-tab `.cpp` is already wrapped in `#if defined(BT_CLIENT)`; no vcxproj changes needed.
- **Static-init order**: registrars across translation units run in unspecified order. `TweaksSliderMap::Get()` must be safe to call from any order — verify it constructs lazily on first call (already true: function-local static).
- **Header propagation**: `TweaksSliderMap.h` is included from each per-tab file already (transitively via `TweaksScreenBase.h`); explicit include should be added per-file for clarity.
- **Label-only edits**: this refactor must not change any string literals. A pure relocation. Run a `git diff` post-refactor to confirm no label drifted.

## Files Touched

- `Engine/Source/Ui/Screens/TweaksScreen/TweaksSliderMap.h` — add `TweaksSliderMapRegistrar` declaration
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksSliderMap.cpp` — add constructor body
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/TweaksScreen/TweaksScreen.cpp` — strip constructor body to `= default`, drop unused `Game.h`/`LightingWrappers.h`/`ParticleWrappers.h`/`SmokeWrappers.h` includes if no longer referenced
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/TweaksScreen/TweaksScreenHexShield.cpp` — add registrar
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/TweaksScreen/TweaksScreenWindDeposits.cpp` — add registrar
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/TweaksScreen/TweaksScreenLightingEffectsVisible.cpp` — add registrar
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/TweaksScreen/TweaksScreenLightingEffectsLighting.cpp` — add registrar
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/TweaksScreen/TweaksScreenParticles.cpp` — add registrar

## Validation

- Build BrokenEngineSandbox client. No new warnings.
- Run, open the tweaks UI in `kbDebugInput` mode, exercise every tab (Hex Shield, Wind→Deposits, Lighting Effects→Visible/Lighting, Particles→Missile/Player/Spaceship). Every slider must respond — silent no-ops indicate a missing registrar entry.
- Diff old vs new label literals: `git grep` each label across both old and new files; counts must match.
