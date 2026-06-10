# TextureCache: Validate Cached Payload Read and Declared Data Size

## Context

`TextureCache::TryLoadCachedTexture` (`Engine/Source/Graphics/Managers/TextureCache.cpp:207-244`) now guards
the **header** read (`!fileStream || magic/version/format/dims/CRC mismatch` at `:224`, landed this session), but the
**payload** read and the header-declared `iDataSize` are still trusted unchecked. A cache file under
`%APPDATA%` is opaque to the loader (a file read is a trust boundary per project policy, so validating it is
policy-compliant — not defensive validation between our own functions), and the on-disk struct
(`TextureFileCacheHeader`, `TextureCache.h:8-22`) carries a caller-independent `int64_t iDataSize` field with no
relationship enforced to the validated dims/format.

Two latent failures, both requiring corruption that passes the magic + version + dims + format gate (medium impact /
low likelihood):

1. **Unchecked payload read** — after the header passes, `data` is sized to `header.iDataSize` and read at
   `TextureCache.cpp:232-233`:
   ```cpp
   std::vector<std::byte> data(header.iDataSize);
   fileStream.read(reinterpret_cast<char*>(data.data()), header.iDataSize);
   ```
   with no `!fileStream` check before the bytes are uploaded. A file truncated after its valid 72-byte header
   uploads partially-zeroed texels (silent corrupt texture, no error).

2. **`iDataSize` never validated against the computed texture size** — the upload lambda at `:237-240`
   ```cpp
   rTexture.UpdateData([&data](void* pData, [[maybe_unused]] int64_t iPosition, int64_t iSize)
   {
       memcpy(pData, data.data(), iSize);
   });
   ```
   copies `iSize` bytes, where `iSize` is the staging-buffer size `Texture::UploadImageData` computes from the
   **validated** dims/format (`Texture.cpp:248-256`, the per-mip `common::SizeInBytes` sum × arrayLayers × depth), while
   `data` is sized to the **file-supplied** `header.iDataSize`. If `iDataSize` is header-valid but smaller than the
   computed size, the `memcpy` overreads past `data`'s buffer (heap overread, UB). A negative `iDataSize` converts to a
   huge `size_t` in the `std::vector` ctor at `:232` → `length_error`/`bad_alloc`.

Single caller is the `GeneratePbrLutBrdf` path (`TextureCache.cpp:156`), which runs at startup / pipeline-recreate, so
the allocation-tracker constraints do not apply here and this code allocates freely. Both `TryLoadCachedTexture` and
`SaveTextureToCache` (`:246-273`) are client-only (file wrapped in `#if defined(BT_CLIENT)`).

## Design

Compute the expected payload size the same way `UploadImageData` does, and gate both the declared `iDataSize` and the
actual read against it. Because the dims/format are already validated against the caller's arguments at `:224`, the
expected size is fully determined by validated data.

1. After the header passes its existing validation (`:224-229`), compute the expected texture byte size from the
   already-validated `header.iWidth/iHeight/iMipLevels/iArrayLayers/vkFormat` using the identical per-mip
   `common::SizeInBytes` accumulation `Texture::UploadImageData` performs (`Texture.cpp:248-256`). Factor that
   loop into a small shared helper (e.g. a free `common::` or `engine::` function `ComputeImageByteSize(format, width,
   height, mipLevels, arrayLayers, depth)`) so the cache validator and `UploadImageData` cannot drift — DRY, and it
   removes the "the two sites compute the same thing independently" hazard that is the root of finding 2. Note `depth`:
   `UploadImageData` multiplies by `mInfo.extent.depth`; the BRDF LUT is depth 1, but the helper must take it so it
   stays a faithful mirror.

2. Reject the file (close + warn + `return false`, identical to the header-mismatch path) when
   `header.iDataSize != <computed expected size>`. This single equality check subsumes both the negative-`iDataSize`
   `std::vector`-ctor blowup (negative ≠ positive expected) and the too-small-`iDataSize` overread (mismatch rejects
   before the `std::vector` is even sized to the bad value). Perform this check **before** the `std::vector<std::byte>
   data(header.iDataSize)` allocation at `:232`.

3. After the payload `read()` at `:233`, add a `!fileStream` check (mirroring the header guard) and reject on a short /
   failed read (truncated-after-header case) before the upload at `:237`.

4. **Cosmetic, fold in:** the `kWarning` LOG at `:227` hardcodes "sourceCrc mismatch" wording for *every* header-reject
   path (truncated header, magic/version/format/dim mismatch all log "sourceCrc mismatch"). Generalize the message to
   name the actual reason or drop the parenthetical to a generic "header validation failed" — keep it a single
   `kWarning` LOG. The new `iDataSize`/payload rejections get their own distinct `kWarning` LOG lines. Follow the
   project LOG-formatting rules: the `{:#x}` specs already present are integer formats (allowed); do not introduce any
   float specs.

## Out of scope

- The header validation at `:224` (magic/version/format/dims/CRC) — landed this session, unchanged here.
- `SaveTextureToCache` (`:246-273`) — it is the producer and writes `iDataSize` from `data.size()`; no validation
  needed on the write side.
- `CopyImageToHostMemory` (`:12-118`) — GPU readback path, not the cache-read trust boundary.
- Any change to `Texture::UploadImageData`'s upload behavior beyond extracting the size loop into the shared helper (the
  lambda/`memcpy` contract stays as-is; the fix is to reject bad input before the lambda runs, not to clamp inside it).
- The `kbRandomlyInvalidatePbrCubemapCache` debug-invalidation path (`:122-130`) — unrelated.
- Adding a payload CRC to the cache format / bumping `TextureFileCacheHeader::kiVersion` — out of scope; size-equality
  validation is sufficient for the identified failure modes and avoids a cache-format churn. (Mention in Notes only.)

## Acceptance criteria

- A cache file truncated after a valid header is rejected (warn + regenerate) instead of uploading zeroed texels.
- A cache file whose `iDataSize` does not equal the size computed from its validated dims/format is rejected before any
  `std::vector` is sized to the bad value — no `length_error`/`bad_alloc`, no `memcpy` overread.
- The expected-size computation is shared with `UploadImageData` (single source of truth), so a future format/mip change
  updates both the uploader and the validator at once.
- A valid cache file still loads (the BRDF LUT round-trips: `SaveTextureToCache` → `TryLoadCachedTexture` accepts).
- The reject-path `kWarning` no longer mislabels every failure as a "sourceCrc mismatch".

## Critical files

- `Engine/Source/Graphics/Managers/TextureCache.cpp` — `TryLoadCachedTexture` (payload guard + `iDataSize` validation +
  LOG wording); the size-computation source it mirrors lives in `UploadImageData`.
- `Engine/Source/Graphics/Objects/Texture.cpp` — `Texture::UploadImageData` (`:245-310`); extract the `:248-256` per-mip
  size loop into the shared helper.
- `Engine/Source/Graphics/Managers/TextureCache.h` — `TextureFileCacheHeader` (read-only reference for the `iDataSize`
  field; no layout change).
- Header for the new shared `ComputeImageByteSize` helper — place beside `common::SizeInBytes` (whichever Common/Engine
  header declares `SizeInBytes`) so both call sites can include it.

## Notes

- Client-only (`#if defined(BT_CLIENT)`); startup/pipeline-recreate only, so no allocation-tracker concern and no
  per-frame hot path.
- No CRC/determinism/network/server exposure — this is a local %APPDATA% cache file, regenerated on reject.
- Size-equality validation (not a payload CRC) is the deliberate choice: it closes both identified failure modes with no
  cache-format change and no `kiVersion` bump, so existing valid caches keep loading. A payload CRC would be a separate,
  larger plan if cache-integrity-against-bit-rot ever becomes a requirement.
- Findings 1 and 2 are fixed together by the same expected-size computation; finding 3 (LOG wording) is a one-word
  cosmetic touch folded in only because the function is already being edited.
