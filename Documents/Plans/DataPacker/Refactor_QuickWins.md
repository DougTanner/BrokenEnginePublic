# Refactor: Quick Wins

Source: /external-refactor-clean on DataPacker/Source/

Goal: small unambiguous cleanups (<15 min each) that remove dead code, dead constants, unreachable branches, or replace preprocessor gates with `if constexpr`.

## Changes

### DataPacker/Source/FileManager.cpp
- Lines 60-64: delete the `if (!std::filesystem::exists(mOutputDirectory))` block. `create_directories` at line 26 + `VERIFY_SUCCESS(std::filesystem::exists(...))` at line 27 already guarantee the directory exists. `VERIFY_SUCCESS` throws on failure in both debug and release (`common::Assert` always throws — see `Common/ErrorUtils.cpp:6-14`), so the MessageBox in this block is unreachable [~5m]

### DataPacker/Source/ExportJobs/ExportIsland.h
- Lines 6 and 8: delete `kiAmbientOcclusionDivisor` and `kiNormalsDivisor` constants — never referenced anywhere in the codebase. `kiElevationDivisor` at line 7 IS used (`ExportIsland.cpp:137`) so keep it [~5m]

### DataPacker/Source/ExportJobs/ExportFont.cpp
- Lines 107-111: the `for (const KerningPair& rKerningPair : pKerningPairs)` loop body is entirely commented out — the loop is a no-op over a `[[maybe_unused]]` binding. Delete the dead loop and the now-unused `pKerningPairs` span (lines 107-112 inclusive) [~5m]

### DataPacker/Source/ExportJobs/ExportShader.cpp
- Line 13 `#define OPTIMIZE_SHADERS`: replace with `inline constexpr bool kbOptimizeShaders = true;` [~10m]
- Lines 127, 173, 205: convert the three `#if defined(OPTIMIZE_SHADERS)` blocks to `if constexpr (kbOptimizeShaders)` — matches C++StyleGuide.txt preference for `if constexpr` over preprocessor gates [~10m]

### DataPacker/Source/ExportJobs/ExportTexture.cpp
- Line 432: fix broken indentation on `if (bFontAtlas)` — closing brace of preceding `for` is at wrong indent relative to the `if` [~2m]

### DataPacker/Source/Texture.h
- Line 20: delete `~Texture() = default;` — redundant under the rule of zero. Members are `int64_t miWidth`, `int64_t miHeight`, `std::vector<std::vector<float>> mData` — all self-managing [~2m]

## Expected Outcome

- Seven small lint-grade cleanups applied mechanically
- No behavior change
- Codebase lint-check baseline improves

## Verification Notes

- Verified `FileManager.cpp:60-64` block is unreachable. `VERIFY_SUCCESS` macro expands to `common::Assert` which unconditionally throws via `std::runtime_error` on failure in both debug and release configurations (confirmed at `Common/ErrorUtils.cpp:6-14`).
- Verified `ExportIsland.h:6-8`. `kiAmbientOcclusionDivisor` and `kiNormalsDivisor` have zero references repo-wide. `kiElevationDivisor` IS referenced at `ExportIsland.cpp:137`. Corrected the bullet to keep line 7.
- Verified `ExportFont.cpp:107-111` loop body is fully commented out.
- Verified `ExportTexture.cpp:432` broken indentation directly.
- Verified `Texture.h:20`. Members are only `miWidth`/`miHeight` (int64_t) and `mData` (`std::vector<std::vector<float>>`) — all RAII. Defaulted dtor is redundant.
- REMOVED the "#if defined(OPTIMIZE_SHADERS) lines 205-240" range claim. There are THREE `#if defined(OPTIMIZE_SHADERS)` blocks (at lines 127, 173, 205) — the third does not extend to 240; exact range should be determined at implementation time. Collapsed to a list of `#if` start lines.
- Tally corrected from "eight" to "seven" cleanups — FileManager, ExportIsland constants, ExportFont loop, OPTIMIZE_SHADERS (define + three conversions count as one logical item), ExportTexture indent, Texture dtor = 7 distinct cleanups.
