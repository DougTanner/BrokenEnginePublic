# TextureFormat SizeInBytes — Fail-Loud Fallback, Overflow Contract, Stale Comment

## Context

`common::SizeInBytes(VkFormat, int64_t iWidth, int64_t iHeight)` (`Common/TextureFormat.cpp:6`) maps a Vulkan format + dimensions to a byte count. It is a pure, stateless helper consumed by the offline DataPacker and the runtime engine. The runtime return value drives memory-safety-critical work: it sizes staging-buffer / VMA allocations, is the size argument to `memcpy` calls, advances per-mip `bufferOffset` values for `vkCmdCopyBufferToImage`, and is used as a divisor in the upload manager (`vkRemainingStaging / iBytesPerChunk`). A wrong return is therefore a memory-safety problem, not a misreport.

Validated against source (`Common/TextureFormat.h`, `Common/TextureFormat.cpp`) — all three findings below cite symbols/lines that exist verbatim. The byte-size groupings in the switch were checked against Vulkan format definitions and are all correct; no additional sizing bug exists.

## Design

Three changes, all confined to the two `TextureFormat` files.

1. **Fail loud on unsupported format** (memory-safety) — `Common/TextureFormat.cpp:39-41`, effort 1.
   - The `default:` case currently does `DEBUG_BREAK(); return 4 * iPixels;`. `DEBUG_BREAK()` only fires with a debugger attached (per `Common/CLAUDE.md` "Validation macros"), so in release an unrecognized `VkFormat` silently returns a *plausible-but-wrong* 4-bytes/pixel guess. Downstream this becomes the `memcpy` size and the staging/VMA allocation size: a too-small guess is an under-allocation → heap-overflow vector; a too-large one over-allocates.
   - Fix: replace the `DEBUG_BREAK(); return 4 * iPixels;` body with `ASSERT(false);` (which `DEBUG_BREAK()`s **and** throws per `Common/CLAUDE.md`). An unsupported texture format reaching this helper is a packer/asset bug — a fatal condition, which matches the codebase's `ASSERT`/throw discipline.
   - **Constraint (do NOT regress):** do **not** change the fallback to `return 0`. The upload manager divides by this return value (`vkRemainingStaging / iBytesPerChunk`), so a `0` fallback trades the wrong-size bug for a divide-by-zero. `ASSERT(false)` (no recoverable return) is the resolution that satisfies both — it terminates the path rather than producing a bogus or zero size.

2. **Document the overflow precondition in the header** (the byte-size accumulation `iPixels = iWidth * iHeight` and the per-format multipliers in `SizeInBytes`) — `Common/TextureFormat.h:6-10`, effort 1.
   - `iPixels = iWidth * iHeight` (`.cpp:8`) and the `2 *`/`4 *`/`8 *` multipliers (`.cpp:25/33/37`) are signed `int64_t`. Signed overflow is undefined behavior (not a defined wrap). This is **not reachable today**: all runtime dimensions derive from `VkExtent` / mip-halving, bounded by Vulkan `maxImageDimension2D` (commonly 16384–32768), so the per-call product stays ~1e9 — orders of magnitude below the ~1.15e18 overflow point of the largest (`8 *`) path. The per-image `* arrayLayers * depth` scaling happens at the call sites, outside this function. This is a contract/robustness gap, not an active bug.
   - Fix: add one header sentence stating the precondition — `iWidth`/`iHeight` are positive and `iWidth * iHeight * bytesPerPixel` must fit in `int64_t`. Per the project "assume parameters are valid" rule this needs no release-time branch. (Optional, low priority: a debug-only `ASSERT(iWidth > 0 && iHeight > 0)` consistent with existing `DEBUG_BREAK()` discipline — include only if it stays a one-liner and does not add release control flow.)

3. **Refresh the stale header comment** — `Common/TextureFormat.h:6-7`, effort 1.
   - The comment claims "compressed formats (BC4, BC7)" and "uncompressed formats (R8, RGBA8, RGBA16F, etc.)" but the switch also handles `VK_FORMAT_BC5_UNORM_BLOCK` (`.cpp:15`) plus `R8G8`, `R16_UNORM`, `R16_SFLOAT`, `R16G16_UNORM`, `R32_SFLOAT`, `R16G16_SFLOAT`, `B8G8R8A8_UNORM`, `R32G32_SFLOAT`. A maintainer adding a format would trust the comment over the switch.
   - Fix: make the comment authoritative and DRY — point to the switch as the source of truth for the supported `VkFormat` set and state that unsupported formats `ASSERT` (after change 1). Do not re-enumerate every format (that just re-introduces drift).

## Critical files

- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\TextureFormat.cpp` — `default:` fallback at `:39-41` (change 1); `iPixels`/multipliers at `:8/25/33/37` (precondition referenced by change 2).
- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\TextureFormat.h` — doc comment `:6-7` (changes 2 + 3), signature `:10`.

## Out of scope

- **Renaming** `common::SizeInBytes` to `TextureSizeInBytes` — speculative API churn touching every caller; violates "don't touch unrelated code."
- **Extracting named constants / `RoundUpToBlocks` helper / per-group `static_assert`s** for the block-byte and bytes-per-pixel literals — cosmetic/maintainability only, KISS-leaning, no defect. (Report M3/L5.)
- **Hoisting `iPixels` into only the paths that use it** — one negligible multiply, micro-cleanliness, no bug. (Report M4.)
- **Negative-dimension runtime validation / per-group bytes-per-pixel comments / wrapping the `:11` rationale comment** — pure input-validation asks and cosmetics. (Report L1/L4/L6.)
- **The direct-include / Pch-reliance note** — conformant by design; the report itself concludes no change. (Report L3.)
- **Any edits to Engine/DataPacker call sites** — call-site behavior is unchanged; `ASSERT(false)` simply removes the silent-wrong-size path.

## Acceptance criteria

- Unsupported `VkFormat` reaching `SizeInBytes` triggers a fatal `ASSERT(false)` in both debug and release — no plausible-but-wrong size is returned, and no `return 0` path is introduced (no downstream divide-by-zero).
- Header documents the positivity + `int64_t`-fits precondition for `iWidth`/`iHeight`/product.
- Header comment no longer omits supported formats (BC5 + the uncompressed set) — it defers to the switch as source of truth rather than re-listing.
- DataPacker and BrokenEngineSandbox (client + server) compile with no new warnings; no behavioral change for any currently-supported format.

## Notes

- `ASSERT` `DEBUG_BREAK()`s and throws with `std::source_location` per `Common/CLAUDE.md`; this is the house fatal path. `DEBUG_BREAK()` alone is a no-op without a debugger — that is exactly why the current fallback is unsafe in release.
- The overflow finding is intentionally documentation-only: it is unreachable under current Vulkan-bounded callers, so a runtime guard would be defensive validation the project rules discourage. Recorded for contract clarity and to prevent a future caller from introducing UB silently.
- Validation found no phantom symbols and no missed sizing bug; the switch groupings are all byte-correct against Vulkan format definitions.
