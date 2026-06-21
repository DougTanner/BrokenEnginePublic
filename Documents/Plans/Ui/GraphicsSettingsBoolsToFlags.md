# GraphicsSettings Bools -> Flags

## Context

Deferred sibling of the landed "Flags over booleans" sweep. That sweep converted clusters of standalone `bool` members to `common::Flags<Enum>` across network/graphics/input structs; those conversions landed mechanically because none of them altered an on-disk layout. The `game::GraphicsSettings` cluster of 7 `bool`s (in `ClientSettings.cpp`) was deliberately left out and split into this scoped plan because, unlike the others, `GraphicsSettings` is a **versioned, serialized POD** round-tripped to disk via `engine::{Write,Read}VersionedFile` into `kAppDataDirectory/GraphicsSettings.bin`. Converting it changes the persisted layout and forces a `kiVersion` bump, so it warranted its own plan rather than an inline edit.

The 7 bools (verified, declaration order, interleaved with non-bool members):

- `bFullscreen`
- `bMultisampling`
- `bAnisotropy`
- `bSampleShading`
- `bSmoke`
- `bWind`
- `bOpaqueUi`

They are interleaved with non-bool members (`VkPresentModeKHR ePresentMode`, `VkSampleCountFlagBits eSampleCount`, and several `float`s such as `fMaxAnisotropy`, `fMinSampleShading`, `fMipLodBias`, `fWaterShapeDetail`, `fSmokeSimulationPixels`, `fSmokeSimulationArea`, `fMinimumAmbient`, `fUiOpacity`, `fUiFontScale`). The struct currently carries `kiVersion = 6`.

Investigation findings that shape the plan:

- The 7 bools are **only ever read from the struct inside `ClientSettings.cpp`** itself — in `SaveGraphicsSettings()` (struct field <- `engine::g*.Get<bool>()`) and `LoadGraphicsSettings()` (`engine::g*.Set(struct field)`). `ResetGraphicsSettings()` touches only the `engine::g*` wrappers, not the struct. Every runtime consumer (Vulkan init in `SwapchainManager`/`PipelineCreator`/`TextureManager`, `Main.cpp` fullscreen, `ImGuiManager` opaque-UI, wind/smoke render uniforms, the `GraphicsMenuScreen` toggles) reads the `engine::g*` `Wrapper` globals, NOT the struct. So the conversion is fully contained to the three `ClientSettings.cpp` functions plus the struct definition.
- `engine::g*` wrapper globals (`gFullscreen`, `gMultisampling`, etc.) are NOT changing — they stay `engine::Wrapper`. Only the in-struct representation changes.
- Serialization mechanism (`Engine/Source/File/FileManager.h`, `Write/ReadVersionedFile`): the writer prepends `int64_t kiVersion` + `int64_t size`, then for a trivially-copyable struct writes the raw bytes (`common::Write`); for a struct with binary stream operators it uses `operator<<`. The reader reads version + size, accepts only when `iVersion == kiVersion` AND (for trivially-copyable) `iSize == sizeof(struct)`; on any mismatch it returns `false` (and `LoadGraphicsSettings` returns `false`).
- **Version-mismatch behavior is a clean reset-to-defaults**: `Main.cpp:140` calls `game::LoadGraphicsSettings()` and ignores its bool return; on a failed/mismatched read the `engine::g*` wrappers simply keep their static-init constructor defaults (e.g. `gFullscreen(true)`, `gOpaqueUi(false)`). No migration path exists, and none is wanted. A `kiVersion` bump therefore safely discards a user's old `GraphicsSettings.bin` and falls back to defaults on first launch after the change.
- Important POD-vs-stream nuance: `common::Flags` has `Write(std::ostream&)`/`Read(std::istream&)` member functions. Adding a `Flags` member makes `GraphicsSettings` **non-trivially-copyable**, so `Write/ReadVersionedFile` switches from the raw-memcpy path to the `has_binary_stream_operators_v` stream path **only if the struct as a whole gains stream `operator<<`/`>>`** (`TweaksSettings` already mixes `Flags` members and is still handled by the trivially-copyable size-check path because `Flags` is itself trivially copyable — it holds a single enum). Confirm at execution whether `common::Flags<GraphicsSettingsFlags>` keeps `GraphicsSettings` trivially-copyable (it should: `Flags` has no non-trivial special members; its `Write`/`Read` are extra methods, not stream operators on the struct). Either way the layout/size changes vs the current 7 separate `bool`s, so `kiVersion` must bump regardless.

## Design

Convert the 7 `bool` members to a single `common::Flags<GraphicsSettingsFlags>`:

1. Add an enum near the struct (in `ClientSettings.cpp`, `game::` namespace):
   ```cpp
   enum class GraphicsSettingsFlags : uint8_t
   {
       kFullscreen    = 1 << 0,
       kMultisampling = 1 << 1,
       kAnisotropy    = 1 << 2,
       kSampleShading = 1 << 3,
       kSmoke         = 1 << 4,
       kWind          = 1 << 5,
       kOpaqueUi      = 1 << 6,
   };
   ```
   7 flags fit in `uint8_t` (`common::Flags` requires an unsigned underlying type).
2. Replace the 7 `bool` members in `GraphicsSettings` with one `common::Flags<GraphicsSettingsFlags> flags {};`. Leave all non-bool members (present mode, sample count, floats) exactly where they are. Pick a member position deliberately (grouping the flag near the top or where the first bool was) — trivial choice, default to where `bFullscreen` sat.
3. Bump `GraphicsSettings::kiVersion` 6 -> 7.
4. Update `SaveGraphicsSettings()`: build the `flags` member with `Set`:
   ```cpp
   graphicsSettings.flags.Set(GraphicsSettingsFlags::kFullscreen,    engine::gFullscreen.Get<bool>());
   graphicsSettings.flags.Set(GraphicsSettingsFlags::kMultisampling, engine::gMultisampling.Get<bool>());
   // ... one per flag
   ```
   (or `Set(e, bExpr)` inline; the designated-initializer block for the struct can keep the non-bool members and assign `flags` afterward, since the per-flag `Set` calls are clearer than one big initializer-list).
5. Update `LoadGraphicsSettings()`: read each via `operator&`:
   ```cpp
   engine::gFullscreen.Set(graphicsSettings.flags & GraphicsSettingsFlags::kFullscreen);
   // ... one per flag
   ```
6. `ResetGraphicsSettings()` needs no change to the bool handling (it only resets the `engine::g*` wrappers and re-saves) — leave it untouched.

### Serialization strategy (single open decision — see Notes)

- **Option (a) — recommended.** Let the new layout serialize naturally (raw POD bytes if the struct stays trivially-copyable, which it should). Bump `kiVersion` 6 -> 7. Rely on the existing version-mismatch path: old `.bin` files (version 6) fail the version check, `LoadGraphicsSettings` returns `false`, and the `engine::g*` wrappers keep their defaults. This is the simplest option and the version-mismatch-resets-to-defaults behavior is confirmed safe (no migration, no user-facing breakage beyond a one-time reset of graphics settings to defaults on the first launch after the change).
- **Option (b) — back-compat alternative.** Keep the 7 bools serialized individually on disk (no layout change, no `kiVersion` bump) while using `Flags` purely in-memory. This requires custom (de)serialization that maps the in-memory `Flags` to/from 7 on-disk bools, i.e. give `GraphicsSettings` explicit stream operators or a hand-written read/write — more code, defeats most of the simplicity win, and is only justified if preserving existing users' saved graphics settings across the upgrade is a hard requirement.

Recommendation: **(a)**, given the confirmed clean reset-to-defaults on version mismatch and the project's KISS directive. Final call deferred to the grill.

## Critical files

- `Projects/BrokenEngineSandbox/Source/ClientSettings.cpp` — the `GraphicsSettings` struct definition, `SaveGraphicsSettings()`, `LoadGraphicsSettings()`, `ResetGraphicsSettings()`, and `kiVersion`. All edits land here; the new `GraphicsSettingsFlags` enum is added here too.
- `Common/Flags.h` — `common::Flags<Enum>` (reference only; `Set(e)/Set(e,bExpr)/Clear(e)`, `operator&(Enum) -> bool`, serializable, unsigned-underlying static_assert). No edit.
- `Engine/Source/File/FileManager.h` — `Write/ReadVersionedFile` (reference only; confirms version+size gating and the trivially-copyable-vs-stream branch). No edit.
- `Engine/Source/Main.cpp` (`:140`) — calls `LoadGraphicsSettings()` and reads `gFullscreen.Get<bool>()` afterward; confirms the ignored-return / default-fallback path. No edit.

## Out of scope

- The other already-landed Flags-over-bools clusters (network/graphics/input structs converted in the prior sweep) — done, not re-touched.
- Any non-bool `GraphicsSettings` members (`ePresentMode`, `eSampleCount`, and all the `float`s) — left exactly as-is.
- The `engine::g*` `Wrapper` globals themselves — they stay `engine::Wrapper`; this plan changes only the in-struct representation.
- The other versioned settings structs in the same file (`SoundSettings`, `TweaksSettings` already uses `Flags`, `ClientStateSettings`).
- The separate `Ui/SpaceshipExplosionLightingDeadSliderRemoval.md` versioned-tweaks question (different blob: LightingWrappers / Tweaks).
- Any broader settings-system redesign, on-disk-format unification, or migration framework.

## Acceptance criteria

- `GraphicsSettings` has one `common::Flags<GraphicsSettingsFlags>` member in place of the 7 separate `bool`s; `kiVersion` bumped to 7 (option a).
- `SaveGraphicsSettings()`/`LoadGraphicsSettings()` round-trip all 7 settings through the `Flags` member; `ResetGraphicsSettings()` unchanged.
- No other file changes (the conversion is contained to `ClientSettings.cpp`).
- Client builds clean; first launch after the change resets graphics settings to defaults (expected, option a) and re-saves a version-7 `.bin`.

## Notes

### State invariant exposure

- **Touches the serialized client-settings file layout + `kiVersion`** — `GraphicsSettings.bin` in `kAppDataDirectory`, version 6 -> 7. Old files reject cleanly and reset to defaults (confirmed: `Main.cpp` ignores the load return; wrappers keep static-init defaults).
- **NOT determinism / CRC sim state** — `GraphicsSettings` is a presentation/quality settings blob, never feeds `FrameInput` or any CRC'd state.
- **NOT `.pack` / replay / network** — unrelated to asset packing, save/replay (`Frame::kiVersion`), or the wire protocol.
- **Client-only** — the whole struct and its functions live under `#if defined(BT_CLIENT)`; allocation-tracked file I/O is already wrapped in `ScopedSuppressAllocationTracking` (`// Heap:`-compliant) in the existing Save/Load functions.

### Pre-staged decisions for /external-grill-plan

1. **Serialization strategy (a) vs (b)** — recommend (a): bump `kiVersion` 6 -> 7, let old saves reset to defaults. Confirm the user accepts a one-time reset of saved graphics settings on upgrade vs requiring back-compat (b).
2. **Confirm version-mismatch behavior** — verified at authoring: `ReadVersionedFile` returns `false` on version OR size mismatch, `LoadGraphicsSettings` returns `false`, `Main.cpp` ignores the return, `engine::g*` wrappers keep static-init defaults. Confirm no migration path is wanted.
3. **Trivially-copyable check** (minor) — confirm at execution that adding `common::Flags<GraphicsSettingsFlags>` keeps `GraphicsSettings` trivially-copyable (expected yes, matching `TweaksSettings`), so it stays on the raw-bytes serialization path; if not, the struct moves to the stream-operator path but the `kiVersion` bump and reset-on-mismatch behavior are unchanged.
