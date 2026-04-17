# Architecture: Unused Includes

Source: /external-architecture-review on Engine/Source/Network + Projects/BrokenEngineSandbox/Source/Network (recursive)

Low-risk cleanup; removes spurious dependencies from the graph.

## Changes

### Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplay.cpp
- Lines 5-8 include `Blasters.h`, `Missiles.h`, `Players.h`, `Spaceships.h`. Only `Players.h` symbols are actually used (via `postRender.pPlayers->...`). Remove `Blasters.h`, `Missiles.h`, `Spaceships.h`. Verify by Grep for `Blasters`/`Missiles`/`Spaceships` symbol usage in the file. [~10m]

## Verification
- After each removal: rebuild the affected project; expect no compile errors.
- If a removal fails, the include was load-bearing — restore and document.

## Verification Notes
Removed the following items from the original plan:
- **`ServerSend.cpp:5`, `ServerReceive.cpp:5`, `Server.cpp:6` — `Memory/MemoryManager.h` removals**: REJECTED. `ScopedSuppressAllocationTracking` is defined in `MemoryManager.h` (confirmed) and all three files use it (e.g., `ServerSend.cpp:22, 60, 183, 219`). `MemoryManager.h` is NOT included by `Engine.h` or `Pch.h`, so removing the direct include breaks compilation. Plan's claim that the symbol "comes via Pch.h/Engine.h" is factually wrong.
- **`ServerTransferManager.cpp:5-8` — removing `Blasters.h`/`Missiles.h`/`Spaceships.h`**: REJECTED. The file references `rDestFrame.postRender.pBlasters->iCount`, `pSpaceships->iCount`, `pMissiles->iCount` at lines 207 and 222, so the includes are load-bearing.

Only the `ReconcileReplay.cpp` item survives (verified no Blasters/Missiles/Spaceships symbol usage beyond the includes themselves at commit d08678d3).
