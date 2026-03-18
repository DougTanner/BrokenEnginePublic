# Tech Debt: Duplicate NetworkSerialization.cpp

Source: /external-tech-debt on Engine/Source/Network

## Changes

### Engine/Source/Network/NetworkSerialization.cpp
- Delete this file — it is dead code. Neither the client nor server vcxproj compiles it; both compile the game-layer copy (`Projects/BrokenEngineSandbox/Source/Network/NetworkSerialization.cpp`) instead. The CLAUDE.md already states "Implementation lives in the game layer" [~2m]

## Verification Notes
- Confirmed: The engine-layer `.cpp` is not listed as a `<ClCompile>` in either `BrokenEngineSandbox.vcxproj` or `BrokenEngineSandboxServer.vcxproj`. Only the `.h` is included.
- The game-layer copy already has the superior `GroupIndicesByType` helper and enum-derived `kiTypeCount` (line 171). The engine copy hardcodes `7` at line 183.
- The original plan suggested porting the engine version's type-validation bounds check (engine line 238) and 1MB sanity cap (engine line 339) to the game version. These were removed because CLAUDE.md directs "DO NOT add error handling or validation — assume parameters to functions are valid."
- No overlap with `NetworkCodeCleanup.txt`.
