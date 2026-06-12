# Refactor: ExportTexture Matching & Routing Mechanics

## Context
Source: /external-refactor-clean on `DataPacker/Source/ExportJobs` (recursive), confirming Phase-1 architecture handoffs. `ExportTexture`'s matching/routing code has per-call allocation churn, substring matching that can misroute on path coincidences, and a cross-domain rescan of the Fonts tree from inside every texture job.

## Design

### `ExportTexture::Handles` allocation churn
- Builds a 12-entry `std::unordered_set<std::string>` (one node allocation per entry) on every directory-entry call, then queries it once (`ExportTexture.cpp:28`). `RunExportJobs` calls this for every file in both input trees. Replace with a function-local `static constexpr std::string_view` array + linear compare [~10m]

### Format sniffing single-pass rework
- `Export` resolves format via eight `mInputPath.native().find(...)` substring tests that match anywhere in the full path — a directory named `Foo.ktx` or `Bar[BC4]` misroutes every file under it — and line 85 re-runs six of the same `find()`s to compute `bRawTexture` (`ExportTexture.cpp:60-87`). Rework: test format-extension cases against `mInputPath.extension()`, the `[BC4]`/`[BC5]`/`[BC7]`/`[C]` tags against `filename()`, and derive `bRawTexture` from the resolved `VkFormat` (true exactly for the explicit-extension formats) — one pass, no duplication [~30m]

### `ProcessLiveCubemap` face-name churn
- Constructs two `std::vector<std::string>` of 6 face names per call and builds each face path via `mInputPath.string().append(...)` temporaries (`ExportTexture.cpp:187-194`). Use `static constexpr const char*` C arrays + `mInputPath / pcFace` [~10m]

### Font-atlas stem-set cache (cross-domain coupling fix)
- `ProcessRegularTexture` runs a `recursive_directory_iterator` over every `Fonts/` tree per texture job to detect font atlases by stem match (`ExportTexture.cpp:222-241`) — N textures × M font files of filesystem traffic, and texture export carries duplicated knowledge of font-layout conventions. Collect the `.fnt` stem set once (function-local `static` built under `std::once_flag` — jobs run concurrently on `std::async` threads) and do a set lookup per job [~30m]

## Critical files
- `DataPacker/Source/ExportJobs/ExportTexture.cpp`

## Out of scope
- Reconciling the `Handles` claimed-extension set with the `bRawTexture` raw-route gate — existing plan `ExportTextureHeaderlessRawFormatClaims.md` owns which extensions belong in the sets; this plan only changes the matching mechanics.
- The zlib/mip-size helper dedup across texture TUs — `Refactor_TextureUtilDedup.md`.
- Adding or removing supported formats.

## Notes
- No pack-byte change for correctly-placed assets; the sniffing rework *does* change behavior for pathological paths (directory names containing `.ktx`/`[BC4]` etc.) — that behavior change is the point. No version bump needed.
- **Dependency**: execute `ExportTextureHeaderlessRawFormatClaims.md` first (or together) — the single-pass rework should be built on the reconciled extension set, not the drifted one.

## Verification Notes
- All citations verified: 12-entry `unordered_set` rebuilt per `Handles` call at :28; format sniffing :60-85 (the chain is 9 `find()` calls across 7 branches — the plan's "eight" undercounts by one, region and substance correct); :85 re-runs six of them for `bRawTexture`; `ProcessLiveCubemap` face vectors/`string().append` at :187-194; font rescan `recursive_directory_iterator` at :222-241 (stem prep :216-221). Jobs run concurrently on `std::async` threads (`Main.cpp:388`), so the `std::once_flag` guard on the stem-set cache is required, as stated.
- Precision on the `bRawTexture` derivation: it must be true for *extension-matched* formats only, not for every resolved `VkFormat` — a `[BC4]`-tagged PNG resolves `VK_FORMAT_BC4_UNORM_BLOCK` via the filename tag but must keep routing to `ProcessRegularTexture` (encode), not the raw passthrough. The single-pass rework already distinguishes extension vs tag matches, so track which case matched and derive `bRawTexture` from the extension case. The plan's parenthetical says this; calling it out explicitly since getting it wrong silently corrupts tagged textures.
- The known `Handles`-set drift (`.R32_SFLOAT` missing; `.R8_UNORM`/`.R8G8B8A8_UNORM`/`.R16G16_UNORM` claimed but decode-routed) is correctly deferred to the pre-existing `ExportTextureHeaderlessRawFormatClaims.md` — no duplication.
