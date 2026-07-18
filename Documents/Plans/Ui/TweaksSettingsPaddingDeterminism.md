# TweaksSettings Persisted-Padding Byte-Determinism

## Context

A review of the sibling graphics-settings persistence fix disproved its out-of-scope claim that `TweaksSettings settings {};` zeroes aggregate padding. Empty-brace aggregate initialization initializes members, but it does not guarantee values for padding bytes. This is a pre-existing sibling defect and is independent of that completed change.

`TweaksSettings` in `Projects/BrokenEngineSandbox/Source/ClientSettings.cpp` is version 13 and currently has one implicit three-byte gap in its 124-byte body:

- `bShowImGui` occupies body offset 0.
- `common::Flags<engine::TweakSectionFlags>` wraps a `uint32_t` and begins at body offset 4, leaving padding at offsets 1-3.
- With `TweakSection::kCount == 12`, the remaining flag, float-array, byte-array, float, and flag members cover offsets 4-123 without another gap.

`SaveTweaksSettings` initializes the aggregate, assigns its semantic members, and passes it to `engine::WriteVersionedFile`. That function falls through to `common::Write`, which persists the complete object representation, including padding. A current `%APPDATA%\Broken Engine Sandbox\TweaksSettings.bin` provides direct evidence: its header reports version 13 and body size 124, while file offsets 17-19 (body offsets 1-3) contain nonzero bytes. Consequently, unchanged semantic settings are not guaranteed to produce identical files and not every persisted representation byte has an initialized value.

## Design

Add an explicit zero-initialized `uint8_t uiPad[3] {}` member immediately after `TweaksSettings::bShowImGui`. The member occupies the existing implicit gap, so `sectionVisible` and all later semantic members retain their current offsets, `sizeof(TweaksSettings)` remains 124, and `kiVersion` remains 13. Existing version-13/size-124 files remain readable: their former padding bytes populate the ignored pad member while every semantic member is read at its existing offset.

Do not change `SaveTweaksSettings` or `LoadTweaksSettings` behavior beyond giving the persisted gap explicit storage. Confirm the actual compiled layout before accepting the change; any unexpected size or semantic-member offset change is a blocker that requires an explicit versioning or migration decision rather than an implicit format change.

## Critical files

- `Projects/BrokenEngineSandbox/Source/ClientSettings.cpp` — `TweaksSettings`, `SaveTweaksSettings`, and `LoadTweaksSettings`.
- `Engine/Source/File/FileManager.h` — `WriteVersionedFile`/`ReadVersionedFile` raw-structure persistence contract (reference only; no edit expected).
- `Common/Serialization.h` — `common::Write` complete-object-representation contract (reference only; no edit expected).
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.h` — `TweakSection::kCount` and `TweakSectionFlags` storage width (reference only; no edit expected).

## Out of scope

- `GraphicsSettings` persistence.
- `SoundSettings`, `ClientStateSettings`, `ReplayMeta`, or a repository-wide raw-serialization audit.
- Redesigning `common::Write` or the versioned settings-file format.
- Changing tweak-section indices, flags width, registry ownership, or saved semantic values.
- A `TweaksSettings` version bump or migration unless implementation evidence proves the layout-preserving change impossible and the user explicitly approves expanded scope.

## Acceptance criteria

- The Debug client target compiles with `TweaksSettings::kiVersion == 13`, `sizeof(TweaksSettings) == 124`, `sectionVisible` still at body offset 4, and every later semantic member at its prior offset.
- A pre-change version-13/size-124 `TweaksSettings.bin` loads through the normal Debug client path without a version/size rejection and applies the same semantic values.
- Two normal saves with unchanged tweak settings produce byte-identical files, including equal whole-file SHA-256 hashes.
- File offsets 17-19 (body offsets 1-3) are `00 00 00` after each save; the header continues to report version 13 and body size 124.
- Runtime verification uses `/agent-harness` when its client workflow can reach the normal tweak-settings save path. Preserve and restore any pre-existing user settings file around verification; if the harness cannot trigger the save, record that limitation and use the normal Debug client shutdown path instead.
- Run the required affected-site scan, C++ correctness review, style review, and Tier-3 adversarial review. Do not add unit tests.

## Notes

- **Tier 3 trigger:** changes the explicit representation of a raw persisted data structure, while preserving its version, size, and semantic offsets.
- Client-only debug settings (`kbDebugInput`); no simulation CRC, replay, wire protocol, `.pack`, server, shader, or allocation-tracked-loop exposure.
- `Ui/Architecture_TweakSectionGameExtension.md` also reaches `TweaksSettings` persistence and may later change its semantic layout. This is ordinary overlap, not a prerequisite: refresh the layout evidence if that plan lands first.
- The completed graphics-settings persistence change shares `ClientSettings.cpp`; no dependency remains.
