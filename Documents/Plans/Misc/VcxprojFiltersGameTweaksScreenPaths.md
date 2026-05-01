# Bugfix: Wrong Filter Paths for Game-Side TweaksScreen Files

## Context

`Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj.filters` lists the game-side `TweaksScreen<Tab>.cpp` group with the wrong source path and wrong filter classification. Around lines 1107-1151 each entry uses:

- Path: `..\..\..\..\Engine\Source\Ui\Screens\TweaksScreen\<File>.cpp`
- Filter: `Engine\Ui\Screens\TweaksScreen`

These files actually live under `Projects\BrokenEngineSandbox\Source\Ui\Screens\TweaksScreen\` and should classify under `Game\Ui\Screens\TweaksScreen` per the rules in `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/CLAUDE.md`:

> Filter paths in `.vcxproj.filters` must mirror the on-disk directory structure:
> `Projects/BrokenEngineSandbox/Source/<path>/File.h` -> filter `Game\<path>`

## Why this is a bugfix not a refactor

Solution Explorer shows these files under the wrong tree node, making them impossible to find by their actual directory. The build itself is unaffected — MSBuild reads `.vcxproj` (which has correct paths), and `.vcxproj.filters` only drives the IDE's tree view. Mechanical fix.

## Affected Files (verify in `.filters` before editing)

The game-only `TweaksScreen<Tab>.cpp` group living in `Projects/BrokenEngineSandbox/Source/Ui/Screens/TweaksScreen/`:

- `TweaksScreen.cpp` and `TweaksScreen.h`
- `TweaksScreenHexShield.cpp`
- `TweaksScreenLightingEffectsLighting.cpp`
- `TweaksScreenLightingEffectsVisible.cpp`
- `TweaksScreenParticles.cpp`
- `TweaksScreenWindDeposits.cpp`

Engine-side files (`TweaksScreenBase.cpp`, `TweaksScreenTest.cpp`, `TweaksScreenPbr.cpp`, `TweaksScreenTerrain.cpp`, `TweaksScreenWaterSpecular/Low/Medium/Debug.cpp`, `TweaksScreenLighting.cpp`, `TweaksScreenShadow.cpp`, `TweaksScreenMisc.cpp`, `TweaksScreenSmoke.cpp`, `TweaksScreenWind.cpp`, `TweaksSliderMap.cpp`) keep their `Engine\Ui\Screens\TweaksScreen` filter and `..\..\..\..\Engine\Source\...` path — those are correct. Do not touch them.

## Changes

For each game-side file listed above, in `BrokenEngineSandbox.vcxproj.filters`:

1. Change `Include` path from `..\..\..\..\Engine\Source\Ui\Screens\TweaksScreen\<File>` to `..\..\Source\Ui\Screens\TweaksScreen\<File>`.
2. Change `<Filter>Engine\Ui\Screens\TweaksScreen</Filter>` to `<Filter>Game\Ui\Screens\TweaksScreen</Filter>`.

Verify the `Game\Ui\Screens\TweaksScreen` filter exists in the `<ItemGroup>` containing `<Filter Include="...">` definitions. If not, add it with a fresh GUID (parent `Game\Ui\Screens` already exists per the file group rule).

Also check `BrokenEngineSandboxServer.vcxproj.filters` for the same files — they should not be present at all in the server filters (entries are wrapped in `#if defined(BT_CLIENT)`); if any are listed, remove them.

## Validation

- Open `BrokenEngineSandbox.sln` in Visual Studio 2026.
- In Solution Explorer, navigate to `Game/Ui/Screens/TweaksScreen` — the six files must appear there.
- `Engine/Ui/Screens/TweaksScreen` must no longer contain any game-side `TweaksScreen<Tab>` entries (only the engine-side ones).
- Right-click one of the moved files, "Open Containing Folder" — it should open `Projects\BrokenEngineSandbox\Source\Ui\Screens\TweaksScreen\`, not the engine path.
- Build BrokenEngineSandbox client and server in Debug, Profile, Release. Build behavior must be identical — this only affects the IDE view.
