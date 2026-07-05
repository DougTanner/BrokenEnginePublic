# GraphicsSettings Persisted-Padding Byte-Determinism

## Context

Follow-up residual from `Save/SaveFileAtomicityAndByteDeterminism.md` (landed). That plan zeroed the *trailing* padding of two raw-byte-serialized POD settings structs (`ReplayMeta`, `ClientStateSettings`) with explicit `uint8_t uiPad[4] {}`. A session-audit of that work found a sibling struct in the same file with the identical byte-nondeterminism class but *interior* padding, which the trailing-`uiPad` pattern does not cover.

`GraphicsSettings` (`Projects/BrokenEngineSandbox/Source/ClientSettings.cpp:74-91`), version 8, persisted raw via `WriteVersionedFile` → whole-struct `common::Write` to `GraphicsSettings.bin`:
- `flags` is `common::Flags<GraphicsSettingsFlags>`; `GraphicsSettingsFlags : uint8_t` (`ClientSettings.cpp:63`), so `flags` is **1 byte** at offset 0.
- The next member `ePresentMode` (`VkPresentModeKHR`, 4-byte enum) is 4-byte-aligned → **3 bytes of interior padding at offset 1-3**.
- `SaveGraphicsSettings` (`ClientSettings.cpp:99-113`) constructs it from a **partial designated-initializer** aggregate (`GraphicsSettings { .ePresentMode = …, … }`, `flags` set afterward via `.Set()`). Non-empty aggregate initialization value-initializes the named/omitted *members* but does **not** zero padding — so the 3 interior bytes are written to disk indeterminate, nondeterministic across saves of identical settings.

Sibling structs were checked and are **already safe** (no action):
- `TweaksSettings` (`ClientSettings.cpp:182-193`): has the same 3-byte interior pad after `bShowImGui` (`TweakSectionFlags : uint32_t`), but `SaveTweaksSettings` constructs it as `TweaksSettings settings {};` (empty-brace value-init zeroes the whole object *including* padding) then member-assigns — padding stays zero.
- `SoundSettings` (`ClientSettings.cpp:15`): 3 floats, no padding.
- `ClientStateSettings` / `ReplayMeta`: fixed by the parent plan.

Impact is **cosmetic**: nothing memcmp-diffs or CRCs `GraphicsSettings.bin`; readers consume by `sizeof` and ignore padding; the save is UI-triggered (not per-frame diffed), so the indeterminate bytes cause no spurious rewrites or functional effect. The only defect is that the persisted file is not byte-reproducible for identical settings — the same latent-wart class the parent plan set out to eliminate for persisted files.

## Design

Fix `SaveGraphicsSettings` so the padding is zeroed. Two equivalent options:

- **Option A (recommended) — construction-site only, zero layout change.** Change the partial designated-init to empty-brace value-init + member assignment, mirroring the already-safe `SaveTweaksSettings` pattern in the same file:
  ```
  GraphicsSettings graphicsSettings {};              // value-init zeroes padding
  graphicsSettings.ePresentMode = engine::gPresentMode.Get<VkPresentModeKHR>();
  … (assign the remaining members) …
  ```
  No struct-layout change, no `kiVersion` bump, old `GraphicsSettings.bin` reads identically.
- **Option B — explicit interior pad member.** Add `uint8_t uiPad[3] {};` after `flags` in the struct. `sizeof` stays 52 (fills the existing interior padding), so no `kiVersion` bump; the aggregate init zero-inits it via its default member initializer. Self-documenting but touches the struct layout.

Recommend Option A: smaller, no struct change, and it makes the two settings-save sites in the file use one consistent construction idiom.

## Critical files

- `Projects/BrokenEngineSandbox/Source/ClientSettings.cpp` — `GraphicsSettings` struct + `SaveGraphicsSettings` (fix); `SaveTweaksSettings` is the reference pattern.

## Invariant exposure

- Client-only (`#if defined(BT_CLIENT)` span). No sim/CRC/wire/determinism-tick exposure. `GraphicsSettings.bin` is a per-client `kAppDataDirectory` local file. `sizeof`/`kiVersion` unchanged under both options → old files load identically. Failure-free success-path change (only the previously-indeterminate padding bytes change, to zero).

## Out of scope

- `TweaksSettings` / `SoundSettings` (verified already deterministic — see Context).
- Any change to the `common::Flags` storage width or the settings file formats.
- DataPacker `.pack`/offline byte-reproducibility (separate, documented-accepted concern).

## Acceptance criteria

- Two saves of identical graphics settings produce byte-identical `GraphicsSettings.bin`.
- Existing `GraphicsSettings.bin` files load unchanged (no version bump).

## Notes

- Grill decision pre-staged: Option A vs B (recommend A — zero layout change, mirrors the sibling `SaveTweaksSettings`).
- Shares `ClientSettings.cpp` with the parent plan's `ClientStateSettings` edit (already landed) — no line-cite dependency remains.
