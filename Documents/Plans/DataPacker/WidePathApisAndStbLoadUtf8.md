# DataPacker Wide Path APIs and stb-load UTF-8

## Context

The engine/game side of the non-ASCII-path correctness sweep landed this session: `engine::FileManager` (temp dir), `engine::Screenshot`, and `game::MainMenuScreen` switched their Windows path lookups from the implicit `-A` (ANSI/active-code-page) entry points to the explicit `-W` ones (`GetTempPathW`, `GetModuleFileNameW`), and the shared stb *write* implementation TU (`ThirdParty/Prebuilts/Source/Engine/Stb.cpp`) now `#define STBIW_WINDOWS_UTF8` so every `stbi_write_*` caller passes filenames as UTF-8 (`std::filesystem::path::u8string()`); both engine-side write call sites were updated.

The DataPacker side was deferred to this plan. DataPacker still has three classes of narrow-path site that mis-decode any path component outside the active code page:

1. **`FileManager::FileManager` ctor** (`DataPacker/Source/FileManager.cpp`) — `GetTempPath` (resolves to `-A`) into a `char pcDirectory[MAX_PATH]`, then assigns into `mTempDirectory` (a `std::filesystem::path`). The narrow temp path is silently re-decoded from the active code page rather than UTF-16. Mirrors the engine `FileManager` fix.
2. **`GetVulkanSdkBinariesDirectory`** (anonymous-namespace static in `DataPacker/Source/ExportJobs/ExportShader.cpp`) — `GetEnvironmentVariable("VK_SDK_PATH", ...)` (resolves to `-A`) into a `char` buffer, then builds a `std::filesystem::path` and `.append("Bin")`.
3. **stb image *load* sites** — `Texture::Texture` (`DataPacker/Source/ExportJobs/Texture/Texture.cpp`, the `FileType::kImage` ctor branch) and `ExportIsland::Export`'s mask-loading loop (`DataPacker/Source/ExportJobs/ExportIsland.cpp`) both call `stbi_load(path.string().c_str(), ...)`. `STBI_WINDOWS_UTF8` is a *separate* define from the write-side `STBIW_WINDOWS_UTF8` and was deliberately NOT set this session — the DataPacker stb *implementation* TU (`ThirdParty/Prebuilts/Source/DataPacker/stb.cpp`, which owns `STB_IMAGE_IMPLEMENTATION`) defines neither. The vendored `ThirdParty/stb/stb_image.h` supports `STBI_WINDOWS_UTF8` (guards `stbi__fopen` on `_wfopen` via `MultiByteToWideChar`, and exposes `stbi_convert_wchar_to_utf8`).

**State invariant exposure: none beyond non-ASCII path decoding.** This is offline DataPacker tooling only. No runtime/engine code, no CRC/determinism sim paths, no `.pack` layout or `kiVersion` change, no replay/network/client-server guard scope, no allocation-tracked path (Tools builds have no allocation tracker — `kbIsDataPacker = true`). Output bytes are byte-identical for any path that is pure ASCII (the overwhelming majority); behavior only diverges for non-ACP path components, which today silently mis-decode.

## Design

### 1. `FileManager` ctor — wide temp path

In `FileManager::FileManager` (`DataPacker/Source/FileManager.cpp`), replace the narrow temp-dir block:

- `wchar_t pwcDirectory[MAX_PATH] {};`
- `DWORD uiTempResult = GetTempPathW(static_cast<DWORD>(std::size(pwcDirectory) - 1), pwcDirectory);`
- `mTempDirectory = pwcDirectory;` (`std::filesystem::path` constructs natively from `wchar_t*` on Windows — no re-decode).

The existing `if (uiTempResult == 0) throw`, the `VERIFY_SUCCESS(std::filesystem::exists(...))`, the `.append("DataPacker")` / `/= mProjectName` chain, and the `LOG(...)` stay as-is.

The ctor does not currently call `GetModuleFileName` or any other `-A` path API (the input/output directories come from `argv` / hardcoded relative literals, then `std::filesystem::canonical`); no additional folds needed in this TU. Confirm at execution by re-reading the ctor.

### 2. `GetVulkanSdkBinariesDirectory` — wide environment variable

In `GetVulkanSdkBinariesDirectory` (`DataPacker/Source/ExportJobs/ExportShader.cpp`), switch to the wide form for consistency with (1) and the engine fixes:

- `wchar_t pwcDirectory[MAX_PATH] {};`
- `DWORD uiResult = GetEnvironmentVariableW(L"VK_SDK_PATH", pwcDirectory, static_cast<DWORD>(std::size(pwcDirectory) - 1));`
- `std::filesystem::path path(pwcDirectory);` (native wide construct).

The `if (uiResult == 0) throw`, `VERIFY_SUCCESS(exists)`, `path.append("Bin")`, and `LOG(...)` are unchanged.

### 3. stb image load — UTF-8 filenames

- Add `#define STBI_WINDOWS_UTF8` (with the same explanatory one-line comment as the write side) **above** `#define STB_IMAGE_IMPLEMENTATION` in `ThirdParty/Prebuilts/Source/DataPacker/stb.cpp` — this is the single TU that owns the stb_image implementation for the DataPacker/ThirdParty.lib build, so the define propagates to `stbi__fopen`. (Does NOT touch the Engine `Stb.cpp` write TU, which already has `STBIW_WINDOWS_UTF8`; the two implementations live in separate object files of the same `ThirdParty.lib`, so the LNK4006 split documented in the DataPacker `stb.cpp` comment is preserved.)
- Switch the two first-party load call sites to pass `u8string()`:
  - `Texture::Texture` (`Texture.cpp`, `FileType::kImage` branch): `stbi_load(rPath.string().c_str(), ...)` → `stbi_load(reinterpret_cast<const char*>(rPath.u8string().c_str()), ...)` (or via a held `std::u8string` local — `u8string()` returns a `std::u8string`; stb's API takes `const char*`, so a `reinterpret_cast` is required, mirroring whatever the engine write-side sites used this session — match that pattern exactly).
  - `ExportIsland::Export` mask loop (`ExportIsland.cpp`): `stbi_load((textureSourceDir / pcMaskNames[i]).string().c_str(), ...)` → the same `u8string()`-based form on the joined path.

No other first-party `stbi_load` / `stbi_loadf` / `stbi_info` call sites exist (repo grep: only these two in first-party code; cmft's internal `stbi_load` is ThirdParty, upstream-pristine, out of scope).

## Critical files

- `DataPacker/Source/FileManager.cpp` — `FileManager::FileManager` ctor temp-dir block (item 1).
- `DataPacker/Source/ExportJobs/ExportShader.cpp` — `GetVulkanSdkBinariesDirectory` (anon-namespace static, item 2).
- `ThirdParty/Prebuilts/Source/DataPacker/stb.cpp` — the `STB_IMAGE_IMPLEMENTATION` TU; add `#define STBI_WINDOWS_UTF8` (item 3).
- `DataPacker/Source/ExportJobs/Texture/Texture.cpp` — `Texture::Texture` `FileType::kImage` branch `stbi_load` (item 3).
- `DataPacker/Source/ExportJobs/ExportIsland.cpp` — `ExportIsland::Export` mask-loading loop `stbi_load` (item 3).
- `ThirdParty/CLAUDE.md` — the stb inventory line currently documents only `STBIW_WINDOWS_UTF8` (write side); extend it to note the load-side `STBI_WINDOWS_UTF8` + `u8string()` convention for DataPacker loaders (one sentence, mirrors the existing write-side phrasing).

## Out of scope

- Any engine/runtime path API — the engine `FileManager`/`Screenshot`/`MainMenuScreen` + write-side `STBIW_WINDOWS_UTF8` siblings landed this session and are NOT revisited here.
- cmft's internal `stbi_load` / `stbi_load_from_memory` (`ThirdParty/cmft/src/cmft/image.cpp`) and any other ThirdParty narrow-path usage — upstream-pristine, never edited (see `ThirdParty/CLAUDE.md`).
- The `argv` / hardcoded-relative input/output directory handling in the `FileManager` ctor — those are not `-A` Win32 path APIs; only the temp-dir lookup is in scope.
- gli/openexr/tinygltf file I/O and `std::fstream` path opens (`std::fstream` already takes `std::filesystem::path` natively) — no narrow-path bug there.
- Broadening the `MAX_PATH` buffer to long-path (`\\?\`) support — orthogonal, not part of the code-page correctness fix.
- Any output-byte / `.pack` / CRC / `kiVersion` change — there is none; pure ASCII paths produce identical output.

## Notes

- The load-side define name is `STBI_WINDOWS_UTF8` (image read), distinct from the write-side `STBIW_WINDOWS_UTF8` (image write) already set in the Engine `Stb.cpp`. Setting it in the DataPacker `stb.cpp` only affects the load implementation linked into `ThirdParty.lib`; it does not collide with or alter the write implementation.
- Match the exact `u8string()` → `const char*` conversion idiom the engine write-side sites used this session (likely `reinterpret_cast<const char*>(...u8string().c_str())` against a held local to keep the temporary alive across the call) so the codebase stays consistent.
- Mechanical, compile-checked, offline tool. No open architectural decision — no `/external-grill-plan` branch to pre-stage.
- Build verification: DataPacker project (and ThirdParty, since `stb.cpp` changes) — both link into `ThirdParty.<Config>.lib` / `DataPacker.exe`.
