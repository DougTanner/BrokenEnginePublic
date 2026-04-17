# Refactor: Workbuffer Push/Pop → RAII

Source: /external-refactor-clean on Engine/Source/Network + Projects/BrokenEngineSandbox/Source/Network (recursive)

Manual `common::Workbuffer::Push()` / `Pop()` pairing appears 26 times in the Network subtree. A small RAII type makes the pairing compiler-enforced and eliminates forget-to-Pop hazards. This plan is a precursor to `Refactor_SendBoilerplate.md` and `Refactor_GuardConsolidation.md`.

## Changes

### Common/Source/(Workbuffer.h)
- Add `ScopedWorkbufferFrame` type (ctor calls `Push`, dtor calls `Pop`, non-copyable, non-movable). Place it alongside `Workbuffer` so `#include "Common/Workbuffer.h"` pulls both. [~15m]

### Engine/Source/Network/Client/ClientSend.cpp
- Migrate 15 Push/Pop pairs at lines 20/53, 64/75, 86/100, 111/122, 167/175, 193/201, 212/220, 231/239, 250/258, 269/275, 286/292, 303/309, 320/326, 337/343, 373/387. Most disappear entirely after `Refactor_SendBoilerplate.md`, but do the RAII migration first to unblock the template. [~30m]

### Engine/Source/Network/Server/ServerSend.cpp
- Migrate 9 Push/Pop pairs at 32/46, 66/78, 84/100, 106/117, 123/131, 139/148, 202/213, 287/293, 331/337. [~30m]

### Engine/Source/Network/Server/ServerReceive.cpp
- Migrate pair at 197/209. [~5m]

### Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp
- Migrate pairs at 288/297, 317/327. [~10m]

### Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.cpp
- Migrate pair at 327/346. [~5m]

### Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp
- Migrate 6 Push/Pop pairs at 397/406, 423/430, 447/455, 472/480, 497/506, 523/532. Same note: most disappear after `Refactor_SendBoilerplate.md`. [~15m]

### Projects/BrokenEngineSandbox/Source/Network/NetworkSerialization.cpp
- Migrate pairs at 241/260, 363/383. [~10m]
- Line 402/417 + 402/423: has conditional `Pop` on LZ4 failure path. Restructure: let the scope handle Pop; change the error branch to plain `return 0`. Verify LZ4 failure path still logs. [~20m]

## Verification Notes
Verified — Push/Pop pair line numbers in `ClientSend.cpp` cross-checked and accurate at commit d08678d3 (20/53, 64/75, 86/100, 111/122, 167/175, 193/201, 212/220, 231/239, 250/258, 269/275, 286/292, 303/309, 320/326, 337/343, 373/387). `ScopedWorkbufferFrame` is genuinely useful precursor for `Refactor_SendBoilerplate`. Note: `ScopedWorkbufferFrame` should go in `Common/Source/Workbuffer.h` (existing path); add to `Common.h` aggregation per root CLAUDE.md convention.
