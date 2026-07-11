# Architecture: Game-Extensible TweakSection Registry

## Context
Carved out of `Ui/Architecture_TweaksRegistryIntegrity.md` (the registry-integrity fixes — duplicate-key detection, collision renames, Day Brightness slider — landed separately). That plan's final item was the `TweakSection` layering concern; at grill the user chose to build a game-extension point rather than accept + document, which turns it from a one-line source comment into a real interface-design task with persistence exposure.

The engine `TweakSection` enum (`Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.h:8-23`) names game-only sections directly — `kHexShield` (:18) and `kParticles` (:21), mirrored in `TweakSectionFlags` (:38, :41). This is the root-AGENTS.md "reverse direction" violation (an engine *type* naming game concepts), unlike `engine::PacketType`'s `kGamePacketStart` opaque extension point. Game consumers: `Projects/BrokenEngineSandbox/Source/Ui/Screens/TweaksScreen/TweaksScreenHexShield.cpp:33`, `TweaksScreenParticles.cpp:64`.

Why it is not a trivial swap — `TweakSection` is the index into, and sizes, multiple structures in `TweaksScreenBase.{h,cpp}`:
- `kpcSectionNames[]` and `kRenderSectionFunctions[]` (member-function-pointer dispatch table) — both `static_assert`'d `== kCount`; game sections are inline entries (`&TweaksScreenBase::RenderHexShieldSection` etc., pure virtuals resolving to the game override).
- Fixed-size arrays `mWindowPositions[kCount]`, `mActiveSubtab[kCount]`, `mPreAuditSubtab[kCount]`.
- `TweakSectionFlags` `uint32_t` bitmask (32-section ceiling) + `SectionFlag()` / `kAllSectionFlags`.
- Every `0..kCount` loop: toggle bar, `RenderSectionWindow` dispatch, the first-open audit.

## Design
This is a **decision/design plan — first step is `/external-design-interface`** on the game section-registration API before any implementation. Design questions to resolve there:
- **Registration shape**: how does the game supply its sections (name + render entry point) to the engine? Options to explore — a game-supplied static section table (span-returning virtual on `GameBase`/`TweaksScreenBase`), a registrar pattern mirroring `TweaksSliderMap`'s anonymous-namespace statics, or a `kGameSectionStart` sentinel with the game owning indices above it.
- **Array sizing**: replace the compile-time `kCount` sizing with `kEngineSectionCount + kMaxGameSections` (a compile-time game cap) or a dynamic container; keep the member-fn-pointer dispatch (game render fns are already pure virtuals) or move to game-provided callables.
- **Flags ceiling**: `TweakSectionFlags` is `uint32_t` (32 sections) — confirm the engine+game total stays under 32 or widen.

## Invariant exposure (the reason this was split out)
**Persistence / versioning.** `game::TweaksSettings` (`Projects/BrokenEngineSandbox/Source/Ui/ClientSettings.cpp`, versioned via `engine::{Write,Read}VersionedFile`) persists section visibility / window positions / active subtab / collapsed state **keyed by the `TweakSection` index** — round-tripped through `SaveState()`/`LoadState()` (`TweaksScreenBase.cpp:321-336`, fixed-size-array `memcpy`s). Splitting the enum into engine-then-game ranges remaps those indices, so existing persisted settings misalign without a `TweaksSettings::kiVersion` bump (or a migration). The design must state the versioning/migration approach. Client-only debug UI — no determinism/CRC/wire/`.pack`/save-game exposure (this is the client settings file, not a game save).

## Critical files
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.h` / `TweaksScreenBase.cpp` (enum, flags, arrays, dispatch, save/load)
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/TweaksScreen/TweaksScreenHexShield.cpp`, `TweaksScreenParticles.cpp` (game section consumers)
- `Projects/BrokenEngineSandbox/Source/Ui/ClientSettings.cpp` (`TweaksSettings` persistence + version)

## Out of scope
- The registry-integrity fixes (duplicate-key detection, collision renames, Day Brightness slider) — already landed via `Ui/Architecture_TweaksRegistryIntegrity.md`.
- Any change to the flat `TweaksSliderMap` keyspace (a separate concern).
- Slider values / tuning.

## Notes
- **Decision plan (present options)** — the `/external-design-interface` pass produces the API shape; do not hand-pick one before it runs.
- Co-schedule / File-Group note: shares `TweaksScreenBase.{h,cpp}` with the other Ui TweaksScreen plans (`Ui/Refactor_TweaksScreenQuickWins.md`, `Engine/TweaksScreenDynamicFontMigration.md`, `Meta/ReviewSweepQuickWins.md`'s `TweakSectionFlags` `static_assert` item) — refresh citations if co-scheduled; the `TweakSectionFlags` ceiling item overlaps this plan's flags-sizing question.
