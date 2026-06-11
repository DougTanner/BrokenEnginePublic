# Architecture: Include Hygiene

## Context

Source: /external-architecture-review on `Engine/Source/Profile`. Two unused includes, one include sitting in the wrong sibling TU, a dead forward declaration, and a split declaration story for `ProfileScreens.cpp`'s five free functions. `ExternalHeaders.h` compliance is clean (zero `#include <...>` across all three files).

## Design

### Engine/Source/Profile/ProfileScreens.cpp
- Remove `#include "Memory/MemoryManager.h"` (`:5`) — neither `giAllocationsThisFrame` nor `EnableAllocationTracking` (the header's only declarations) is referenced in this TU. [~2m]
- Replace `#include "Game.h"` (`:8`) with `#include "Graphics/Camera.h"` — the only game symbol used is `game::gpCamera` (`:144`, `:226`), declared in the game `Graphics/Camera.h:79`; `Game.h` does not provide it (it arrives today via PCH → `Engine.h:96` → `GameBase.h:6`). Drops the ClientSettings/Fleet/FleetSelection/ServerSession/GameSaveLoad/ClientSession closure from this TU's direct dependencies. [~3m]
- Delete the local forward declarations of `FormatFpsHeader`/`FormatCpuScreen`/`FormatGpuScreen` in the consumer TU (`ProfileManagerBase.cpp:13-17`) once they move to the header (below). [~2m]

### Engine/Source/Profile/ProfileManagerBase.cpp
- Add `#include "Memory/MemoryManager.h"` — this TU uses `giAllocationsThisFrame` (`:118`, `:145`, `:397`) yet today receives it only through an accidental game-PCH chain (`Pch.h:96` `Frame/Frame.h` → `FrameBase.h` → `AreaLights.h` → `Collection.h:175` → `CollectionMemory.h:3` → `Memory/MemoryManager.h`). The include currently sits in the sibling TU that doesn't need it. [~3m]

### Engine/Source/Profile/ProfileManagerBase.h
- Delete the dead `class DeviceManager;` forward declaration (`:6`) — referenced nowhere in the header. [~2m]
- Declare `FormatFpsHeader`/`FormatCpuScreen`/`FormatGpuScreen` beside the existing `FormatCpuTimersText`/`FormatCpuCountersText` declarations (`:375-376`), inside a `BT_CLIENT` guard — today three of `ProfileScreens.cpp`'s five functions are re-declared ad hoc in a consumer TU, so signature drift is caught only at link time. [~8m]
- Add a guard comment at the top of the header: it is `Engine.h`'s position #1 (`Engine.h:4`) and must stay engine-include-free (its upstream closure is Common + ExternalHeaders only); any engine include added here becomes an aggregation-order landmine. [~3m]

### Engine/Source/Profile/CLAUDE.md
- Fix the `:37` claim that the `gpCamera` read comes via "include of `Game.h`" — after this plan it comes via the explicit `Graphics/Camera.h` include. [~2m]

## Critical files
- Engine/Source/Profile/ProfileManagerBase.h
- Engine/Source/Profile/ProfileManagerBase.cpp
- Engine/Source/Profile/ProfileScreens.cpp
- Engine/Source/Profile/CLAUDE.md

## Out of scope
- Creating a separate `ProfileScreens.h` — header consolidation is the cheaper shape (two of the five functions already live in `ProfileManagerBase.h` for the server display).
- Moving `Ui/GraphicsSettingsWrappersBase.h` (`ProfileScreens.cpp:6`) inside a `BT_CLIENT` guard — marginal server-build win; wrapper headers are deliberately direct-included (Ui convention).
- The repo-wide `Memory/MemoryManager.h` unused-include sweep (`Engine/DeadCodeAndUnusedIncludesSweep.md`) — this plan supersedes it for the two Profile TUs (move, not delete).

## Notes
- Includes/declarations only; compile-checked in both builds. No determinism/CRC/network/`kiVersion` exposure.
- Coordinate with `Engine/DeadCodeAndUnusedIncludesSweep.md` (its `MemoryManager.h` grep will catch `ProfileScreens.cpp`) — annotate in `Order.md` Dependencies when the row is added.
- Land before or with `Architecture_ClientServerGuardScope.md` (its hoisted `giAllocationsThisFrame` latch relies on this plan's direct include rather than the PCH accident).

## Verification Notes
All items verified against source (2026-06-10 pass):
- `Memory/MemoryManager.h` declares exactly two symbols (`giAllocationsThisFrame`, `EnableAllocationTracking`); `ProfileScreens.cpp` references neither — removal valid. `ProfileManagerBase.cpp` uses `giAllocationsThisFrame` at `:118`/`:145`/`:397` with no direct include — addition valid.
- Transitive chain verified link-by-link: `Pch.h:96` `Frame/Frame.h` → `:3` `Frame/FrameBase.h` → `:5` `Frame/Collections/AreaLights/AreaLights.h` → `:5` `Frame/Collections/Collection.h` → `:175` `CollectionMemory.h` → `:3` `Memory/MemoryManager.h`. (`Engine.h` itself never includes `MemoryManager.h`.)
- `game::gpCamera` is the only `game::` symbol in `ProfileScreens.cpp` (`:144`, `:226`); declared at game `Graphics/Camera.h:79`. The file is whole-`BT_CLIENT`-wrapped, so the include is inert in the server build, and no `Engine/Source/Graphics/Camera.h` exists to shadow the resolution. `Game.h`'s include list (ClientSettings / Data/Audio / Fleet / FleetSelection / per-build Session+SaveLoad headers) confirmed to not provide it; today's provider is `Pch.h:97` `Engine.h` → `:96` `GameBase.h` → `:6` `Graphics/Camera.h` as claimed.
- `ProfileManagerBase.h` has zero `#include` lines and is `Engine.h`'s first include (`:4`); `class DeviceManager;` (`:6`) is referenced nowhere else in the header. The three ad-hoc forward declarations sit at `ProfileManagerBase.cpp:13-17` inside a `BT_CLIENT` guard — the new header declarations must carry the same guard (the plan already says so).
- Overlap with `Engine/DeadCodeAndUnusedIncludesSweep.md` item 12 confirmed and correctly partitioned: that item's first half (direct include into `ProfileManagerBase.cpp`) is this plan's item; the `CollectionMemory.h:3` include drop stays with the sweep, and the sweep explicitly tolerates partial landing ("can land any subset").
- No caveats.
