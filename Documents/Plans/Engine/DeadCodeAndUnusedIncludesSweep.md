# Dead-Code and Unused-Include Sweep (Engine / Game / Shaders)

## Context

The repo-wide CLAUDE.md refresh surfaced a cluster of small dead-code and unused-include findings, none
individually plan-worthy but collectively a coherent cleanup pass. Each is grep-verifiable and low-risk
(removal of unreferenced symbols/includes). Grouped here because they are the same kind of work; each item is
independent, so the sweep can land any subset.

### Unused includes

1. **~12 Engine TUs include `Memory/GlobalAllocator.h` without using its symbols** — leftover from
   `ScopedSuppressAllocationTracking` moving to `Common/AllocationTracking.h`. For each, confirm via grep that
   no `GlobalAllocator.h` symbol (the allocation-track API, operator overloads) is referenced in the TU, then
   drop the include. Per the project rule, only remove includes whose unused-ness *this work* establishes — but
   here the move already orphaned them. (Identify the ~12 by grepping for `#include "Memory/GlobalAllocator.h"`
   and cross-checking against actual symbol use.)

12. **`Frame/Collections/CollectionMemory.h:3` includes `Memory/GlobalAllocator.h` (header-level leftover of the
    same move as item 1)** — its only allocation symbol is `ScopedSuppressAllocationTracking`
    (`CollectionMemory.h:127/:186/:229/:282/:340/:358/:377`), which lives in `Common/AllocationTracking.h:6`.
    Unlike item 1's TUs, this is a *header* and is the accidental channel distributing `GlobalAllocator.h` to
    every TU in both projects (Pch.h:96 → game `Frame.h:3` → `FrameBase.h:8` → `Explosions.h:3` →
    `Collection.h:175` → `CollectionMemory.h:3`). **Ordering pair**: `Profile/ProfileManagerBase.cpp` uses
    `giAllocationsThisFrame` (`:118/:145/:397`) with **no** direct include — it survives only via this chain.
    Add `#include "Memory/GlobalAllocator.h"` to `ProfileManagerBase.cpp` *first*, then drop the
    `CollectionMemory.h:3` include. (Surfaced by the Input/Memory `/external-deep-analysis` run.)

### Dead declarations / unused methods

10. **`Collection.h:18` forward-declares `struct FrameBase;`** — there is no `struct`/`class FrameBase` anywhere
    in the repo (only `FrameInterpolateBase` / `FramePostRenderBase` exist, and `FramePostRenderBase` is already
    forward-declared on the next line). Stale forward declaration; remove the `struct FrameBase;` line. Confirm
    via grep that nothing in this TU references a `FrameBase` type before deleting (the surrounding forward decls
    `FramePostRenderBase` / `GridCoord` / `Wrapper` stay).
11. **`game FrameInput::operator==` (`Input.h:60`) has no callers** anywhere in the repo. The
    `DifferenceStream` identical-diff skip compares `Crc()` values (`DifferenceStream.h:56`:
    `rDifference.Crc() == mCurrentDifference.Crc()`), not `operator==`, and reconciliation desync detection
    compares `Frame` CRCs (game `Input/CLAUDE.md` documents both). Remove the unused `operator==` after a
    grep confirms zero call sites (the only `== `/`FrameInput` co-occurrences are `mFrameInputs.end()` iterator
    comparisons, not value comparisons). Sibling of item 3 (`GetGamepadMode()` no-caller removal). Plain
    method removal — not a SOA member, no `kiVersion`/CRC exposure (the `statusChanges` field it reads stays).
13. **`Collection.h:25-33` forward-declares 8 graphics types never referenced** — `Buffer`, `BufferManager`,
    `CommandBufferManager`, `ModelPipeline`, `PipelineManager`, `SwapchainManager`, `TextureManager`, and
    `enum class CommandBufferFlags`. None is referenced in `Collection.h` itself, nor in **any header** in either
    Collections tree (engine or game — grep `*.h` hits only the declaration lines). The `*Render.cpp` TUs that do
    name `Buffer`/`TextureManager` require complete types, provided by the PCH's `Engine.h` — the forward decls
    are never load-bearing. Likely residue from when render hooks took manager parameters (current hooks take
    `int64_t iCommandBuffer`). Remove the 8 declarations; **keep** the used decls around them: `game::` frame
    types at `:3-11` (load-bearing for downstream collection headers, e.g. `FrameBase.h`/`Players.h`),
    `FramePostRenderBase` `:19`, `GridCoord` `:20`, `Wrapper` `:21-23` (used by `ControllerType` at `:335-338`).
    (Surfaced by the Collections `/external-deep-analysis` run; sibling of item 10, same header.)
14. **`Collection.h:13` `using VkDeviceSize = uint64_t;`** — global-scope redeclaration of Vulkan's typedef,
    which every TU already receives via `Common/ExternalHeaders.h:230` (`<Volk/volk.h>`, included
    unconditionally — server build too). Identical redeclaration is legal but dead weight: `VkDeviceSize` is not
    used in `Collection.h`/`CollectionMemory.h` or any Collections header; the nearby uses
    (`PlayersRender.cpp:76`, `IslandTerrain.cpp:438`) are TUs where the real typedef is in scope. Remove the
    alias. **MEDIUM confidence** — verify with full client + server builds (the alias may be masking a TU that
    somehow predates volk in include order; none found by grep). (Surfaced by the Collections
    `/external-deep-analysis` run.)

### Dead local variables / fields

2. **Explosions write-only client fields** — `pfLightPercents` / `SpawnInfo::fLightPercent` and the persisted
   `pfSmokePercents` array are write-only client-side state (set, never read to drive anything). Removal
   candidates. **NOTE:** if any of these are in a collection's `SharedMembers`/`Members` (CRC/serialized), they
   are **not** part of this lightweight sweep — they must route through the `add-collection-member` checklist
   with a `kiVersion` bump (like `MissilesDeadExplosionRadiusField.md`). Verify CRC/serialization membership
   first; pull any CRC-participating field **out** of this plan into its own member-removal plan.
3. **`game Input::GetGamepadMode()` has no callers** anywhere in the repo — dead method; remove it (and its
   declaration). Verify zero call sites first.
4. **Engine `Network/Client/Client.h` unused `game::player_t` alias** (`id_t<PlayersInterpolate>`) — apparently
   unused, and an engine header naming a `game::` alias is also a layering smell. Confirm no users; if it
   belongs anywhere it is the game layer, not an engine header — remove from `Client.h` (relocate only if a
   user is found).
5. **`ServerSend.cpp` `SendResends`: `aiResendTicks` filled but never read** — leftover from a removed log.
   Remove the array and its fill loop.
6. **`HexShield.vert:65`: dead local `float fDirection = 0.0f;`** never read — remove.
7. **`QuadsAxisAlignedVisibleArea.vert`: forwarded location-6 `(cos, sin)` varying consumed by no fragment
   shader** — dead cross-stage varying. Remove the output (and the matching `layout(location=6)` input if any
   frag declares but ignores it). Shader-only; requires a DataPacker recompile. (Cross-reference: the report's
   "Quads" shader note already flagged location-6 as "currently unconsumed".)
8. **`RawInputManager.cpp`: mouse `RIDEV_INPUTSINK` registration may be removable** — mouse raw-input packets
   reaching `HandleRawInput` are discarded (DirectXTK Mouse is fed by legacy WndProc messages, not WM_INPUT).
   **Behavior-verify before removing** — this one is not pure dead code; it changes what the OS delivers.
   Confirm no path consumes raw mouse packets, then drop the mouse RIDEV registration (keep keyboard/other
   registrations). If uncertain, leave it and document why instead. (Lowest-confidence item in the sweep;
   consider deferring to its own playtest-gated plan if the grill is unsure.)

### Possibly-unused `SharedMembers`

9. **`SharedMembers()` on `SpawnPlayerData`/`UpdatePlayerData`/`UpdateFleetData` appears unreferenced** —
   those structs use a defaulted `operator==`; only `TransferData::SharedMembers` is consumed. Confirm via grep
   that nothing calls these three `SharedMembers`, then remove them. (These are plain data structs, not SOA
   collections, so no `kiVersion`/CRC-layout concern — but double-check they are not pulled into a
   build-shared-equality path before deleting.)

## Design

Treat each item as an independent removal:

1. Grep for the symbol/include to confirm zero (gameplay/CRC) consumers in the current tree.
2. Remove the declaration + any orphaned definition/fill/store.
3. For shader items (7), rebuild via DataPacker and confirm the affected shaders still compile and render.
4. For the two behavior-adjacent items (8 raw-input, and any CRC-participating Explosions field in 2), do **not**
   fold them into the mechanical sweep — either playtest-gate them or split them out. The grill should decide
   per-item: confident-dead → remove inline; behavior-adjacent → separate/deferred.

This is mechanical and compile-checked except where noted. Keep each removal narrow (only the symbol and its
directly-orphaned wiring) per the "don't touch unrelated code" directive.

## Out of scope

- **Any CRC-participating / serialized field** (Explosions fields that turn out to be in `SharedMembers` /
  `Members`) — those require the `add-collection-member` removal checklist + `kiVersion` bump and a dedicated
  plan; do not delete them in this lightweight sweep.
- `GlobalAllocator.h`/`AllocationTracking.h` themselves — only the *consumers'* stale includes are touched.
- The `RIDEV_INPUTSINK` removal if behavior-verification is inconclusive — defer rather than guess.
- Stale code *comments* (the FileManager "pre-faulted" comments, the terrain indirect-draw comments, etc.) —
  those are a separate `StaleCodeCommentsCleanup` plan.
- Functional changes — every item here is removal of unreferenced code, not a behavior change (except the
  explicitly-flagged raw-input registration, which is gated/deferred).

## Acceptance criteria

- Each removed symbol/include/varying has zero remaining references (grep-clean) and the affected client +
  server projects build clean.
- Shader item (7) rebuilds via DataPacker and the affected shaders still compile/link/render.
- No CRC/serialized field was removed without a `kiVersion` bump (any such field was split out to a
  member-removal plan instead).
- The `RIDEV_INPUTSINK` change (8), if taken, is playtest-confirmed to not break mouse input; otherwise it is
  deferred with a documented reason.

## Critical files

- ~12 Engine TUs with stale `#include "Memory/GlobalAllocator.h"` (enumerate via grep at execution).
- `Engine/Source/Frame/Collections/CollectionMemory.h` (`:3` stale include — item 12) paired with
  `Engine/Source/Profile/ProfileManagerBase.cpp` (gains the direct include — item 12).
- `Engine/Source/Input/RawInputManager.cpp` — mouse `RIDEV_INPUTSINK` registration.
- `Engine/Source/Network/Client/Client.h` — `game::player_t` alias.
- `Engine/Source/Network/Server/ServerSend.cpp` — `SendResends` `aiResendTicks`.
- Explosions collection (engine `Frame/Collections/Explosions/*` and/or game side) — `pfLightPercents` /
  `SpawnInfo::fLightPercent` / `pfSmokePercents` (verify CRC membership first).
- `Projects/.../Source/Input/*` — `game Input::GetGamepadMode()`.
- `Engine/Data/Shaders/HexShield.vert` (`:65`), `Engine/Data/Shaders/.../QuadsAxisAlignedVisibleArea.vert`
  (location-6 varying).
- The player/fleet spawn/update data structs declaring the unreferenced `SharedMembers` (grep
  `SpawnPlayerData`/`UpdatePlayerData`/`UpdateFleetData`).
- `Engine/Source/Frame/Collections/Collection.h` (`:18` stale `struct FrameBase;` forward decl — item 10;
  `:25-33` dead graphics forward decls — item 13; `:13` redundant `VkDeviceSize` alias — item 14).
- `Projects/BrokenEngineSandbox/Source/Input/Input.h` (`:60` unused `FrameInput::operator==` — item 11);
  read-only reference `Engine/Source/File/DifferenceStream.h` (`:56`, the `Crc()`-based identical-diff skip).

## Notes

- All items grep-verifiable; the sweep can land any subset. Quick Win in aggregate; near-zero risk except the
  two behavior-adjacent items called out above.
- No determinism/CRC/version exposure **as long as** CRC-participating fields are routed out to a member-removal
  plan rather than deleted here.
- The shader varying removal (7) requires a DataPacker recompile (not just a C++ build).
