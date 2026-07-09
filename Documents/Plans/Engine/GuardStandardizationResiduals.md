# Guard Standardization Residuals

## Context

A session standardized whole-file `#if defined(BT_CLIENT)` / `#if defined(BT_SERVER)` guards for single-build files (root CLAUDE.md Client/Server Targets rule updated: single-build files carry a whole-file guard, guard placed *first* with `#include`s inside it). Three sites remain non-conforming; each was blocked or skipped for a stated reason during that session and is captured here.

## Design

Fix each of the three sites to match the standardized convention (whole-file guard, guard-before-include):

- **a. `Projects/BrokenEngineSandbox/Source/Ui/HexShieldWrappers.{h,cpp}`** — left unwrapped because game `Pch.h` includes the header unconditionally (`Pch.h:100`, `#include "Ui/HexShieldWrappers.h"`), so both builds compile it; its header has no guard (`#pragma once` at `HexShieldWrappers.h:1`) and the `.cpp` opens with a bare `#include "HexShieldWrappers.h"` (`HexShieldWrappers.cpp:1`). Sole reader is engine `MainUniforms.cpp` (itself `BT_CLIENT`-wrapped) — a session sweep found no server use. Fix: gate the `Pch.h:100` include with `#if defined(BT_CLIENT)`, then whole-file-wrap both `HexShieldWrappers.h` and `.cpp`, and drop `HexShieldWrappers` from the server vcxproj (client-only). Update the `Ui/CLAUDE.md` "HexShieldWrappers is the one exception" note and the game `Source/CLAUDE.md` Pch.h-provided-headers list accordingly.
- **b. `Engine/Source/Network/Server/ServerSessionBase.h` — RESOLVED as documented-accepted; contradiction surfaced.** The `class NetworkDiscoveryResponder;` forward declaration sits *outside* the `#if defined(BT_SERVER)` guard (`ServerSessionBase.h:6`; the guarded `class ServerSessionBase` spans `:8-28`). A parallel session added a doc-exception accepting this as benign — `Server/CLAUDE.md:14`: "`ServerSessionBase.h` keeps its `class NetworkDiscoveryResponder;` forward declaration above (outside) the whole-file `BT_SERVER` guard — a benign partial guard; the file is server-vcxproj-only and reached only by server TUs, so the stray decl never reaches the client." That decision (accept + document) **contradicts** this plan's original intent to move the decl inside / wrap whole-file. Authority resolution: the just-landed doc exception is the current accepted state, so site b is **not an action item** — leave the partial guard as documented. Only revisit if a future rule requires strict whole-file conformance here; if so, moving the one-line decl inside the guard is the fix. Retained in this plan only as the record of the contradiction, not as work to do.
- **c. `Engine/Source/Graphics/Islands.cpp`** — pre-existing: places `#include "Islands.h"` at `Islands.cpp:1`, *before* its whole-file `#if defined(BT_CLIENT)` guard at `:3`, unlike the guard-first convention. Fix: move the include inside the guard.

Re-verify line cites at execution; refresh against current source.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Ui/HexShieldWrappers.h` / `HexShieldWrappers.cpp`
- `Projects/BrokenEngineSandbox/Source/Pch.h` (`:100` include)
- `Projects/BrokenEngineSandbox/Source/Ui/CLAUDE.md`, `Projects/BrokenEngineSandbox/Source/CLAUDE.md` (doc updates for site a)
- `Engine/Source/Network/Server/ServerSessionBase.h`
- `Engine/Source/Graphics/Islands.cpp`
- Client/server vcxproj + `.filters` pairs (HexShieldWrappers server-side removal, site a)

## Out of scope

- The two deliberately-unwrapped wrappers `ParticleWrappers` / `WindDepositsWrappers` (both-vcxproj by design — unguarded server frame code references their symbols; `Ui/CLAUDE.md`).
- Any behavior, symbol, or layout change — this is guard/include placement only.
- The `Engine.h` `BT_CLIENT`/`BT_SERVER` aggregation spans (include grouping only; not the affinity mechanism).

## Acceptance criteria

- Each of the three files is whole-file guarded with the guard preceding all `#include`s.
- `HexShieldWrappers` is removed from the server vcxproj/filters; client build unaffected.
- Client and server both compile.

## Notes

- No CRC / wire / `kiVersion` / determinism exposure; entirely compile-checked.
- **Site b**: now a documented-accepted exception (`Server/CLAUDE.md:14`), not actionable — see the Design bullet for the surfaced contradiction. `Network/Refactor_SessionBaseCollapse.md` deletes `ServerSessionBase.*` outright anyway, so the partial guard disappears when that lands.
- **Scope note**: with site b accepted, the actionable work is sites a and c only.
- **Site c interaction**: `Islands.cpp` is a File-Group file (`TerrainInstanceAreaCull` / `TerrainMeshLodChain` / island-residency plans edit its per-frame SSBO/indirect region). Moving the top include is a small top-of-file line shift only — refresh those plans' `Islands.cpp` cites if co-scheduled.
