# ExportTexture::Handles Claims Raw Formats Its Decode Path Can't Read

## Context

`ExportTexture::Handles` (`DataPacker/Source/ExportJobs/ExportTexture.cpp:21-58`) advertises a set of input
extensions the texture exporter will process:

```cpp
std::unordered_set<std::string> extensionSet = {".png", ".tga", ".jpg", ".ktx",
    ".BC4_UNORM_BLOCK", ".BC5_UNORM_BLOCK", ".BC7_UNORM_BLOCK",
    ".R8_UNORM", ".R8G8B8A8_UNORM", ".R16_UNORM", ".R16G16_UNORM",
    ".R16G16B16A16_SFLOAT"};                                                    // :28
```

But the export body's **raw-route gate** (`bRawTexture`, `:85`) only treats a *subset* of these as headerless
raw files:

```cpp
bool bRawTexture = ...find(L".R16_UNORM") || ...find(L".R32_SFLOAT") || ...find(L".BC4_UNORM_BLOCK")
                || ...find(L".BC5_UNORM_BLOCK") || ...find(L".BC7_UNORM_BLOCK") || ...find(L".R16G16B16A16_SFLOAT");
```

The three extensions **claimed by `Handles` but absent from `bRawTexture`** — `.R8_UNORM`,
`.R8G8B8A8_UNORM`, `.R16G16_UNORM` — therefore fall through to the **image-decode path** (the stb/PNG-style
loader for `.png`/`.tga`/`.jpg`), which expects a decodable image **header**. A genuinely headerless raw blob
named `Foo.R8_UNORM` (no PNG/TGA header) routed through the decode path will fail to decode — a latent trap: the
extension is advertised as supported but cannot actually be ingested as raw.

(Note the inverse mismatch too: `bRawTexture` lists `.R32_SFLOAT` which is **not** in `Handles`' set — so a
`.R32_SFLOAT` input is never claimed by `Handles` in the first place; it only matters via the format-selection
branch at `:64-66`. That is a separate inconsistency worth confirming in the same pass.)

So one of two things is true and the code does not say which:

- **Dormant support**: nobody ships `.R8_UNORM`/`.R8G8B8A8_UNORM`/`.R16G16_UNORM` raw inputs, so the three
  extensions in `Handles` are aspirational/dead and the decode-path mismatch never fires. Then the fix is to
  **drop them from the `Handles` set** (don't advertise what can't be ingested) or wire them into the raw
  route.
- **Latent bug**: such inputs are (or will be) authored, and they will silently fail decode. Then the fix is to
  **add them to `bRawTexture`** with correct element sizes (R8 = 1 byte/texel, R8G8B8A8 = 4, R16G16 = 4) and a
  matching `VkFormat` selection branch.

## Design

This is a **dormant-support-vs-latent-trap decision** — confirm intent in the grill, then converge `Handles`
and the raw-route gate so they agree:

- **If the three formats should be raw-ingestible (latent bug):** add `.R8_UNORM`, `.R8G8B8A8_UNORM`,
  `.R16G16_UNORM` to the `bRawTexture` predicate (`:85`) and to the `VkFormat` selection (`:59-66`, alongside
  the existing `.R32_SFLOAT`/default branches), with the correct bytes-per-texel for each so the raw size math
  is right. Verify nothing else in the export body assumes a decoded-image source for these.
- **If they are not used (dormant/dead):** remove the three extensions from the `Handles` `extensionSet`
  (`:28`) so `Handles` only claims what the body can actually process; this makes the advertised set honest and
  prevents a future author from being silently dropped into the broken decode path. Simplest; recommended if a
  repo/asset-tree search finds no `.R8_UNORM`/`.R8G8B8A8_UNORM`/`.R16G16_UNORM` source files.
- **Reconcile `.R32_SFLOAT`**: it is in `bRawTexture` and the `VkFormat` branch but **not** in `Handles`. If
  `.R32_SFLOAT` inputs are intended, add it to the `Handles` set so it is claimed; if not, the `bRawTexture` /
  format references to it are dead and can be dropped. Resolve in the same grill (one consistent set across
  `Handles` ↔ `bRawTexture` ↔ format-selection).

Recommendation: search the `Data/` asset tree first. The likely outcome is **dormant** for the three (drop from
`Handles`) — but the grill should confirm against the actual authored assets before deleting, since the cost of
a wrong delete is "a real raw input stops being packed."

## Out of scope

- The image-decode path itself (stb/PNG loading) and the BC-block / `.ktx` / `.R16_UNORM` / `.R16G16B16A16_SFLOAT`
  raw routes that already work — unchanged.
- The texture intermediates/RDO/compression pipeline — only the `Handles` claim vs raw-route gate is in scope.
- The `.pack`/`.manifest` format and `kiTextureIntermediateMagic` — no format change.
- The other `Export*::Handles` (audio, model, scene, shader, island, font, raw) — only `ExportTexture` has the
  claim/route mismatch.
- Adding new texture formats not already mentioned in either list.

## Acceptance criteria

- `ExportTexture::Handles`' claimed extension set and the export body's raw-route gate (`bRawTexture`) +
  `VkFormat` selection **agree**: every extension `Handles` claims is actually ingestible by the body, and
  every extension the raw route handles is claimed by `Handles`.
- A headerless `.R8_UNORM`/`.R8G8B8A8_UNORM`/`.R16G16_UNORM` input either packs correctly (if wired into the
  raw route) or is no longer advertised as supported (if dropped) — no silent decode failure.
- `.R32_SFLOAT`'s presence is consistent across `Handles`/`bRawTexture`/format-selection.
- DataPacker builds and a full bake of the current asset tree succeeds with no newly-broken textures.

## Critical files

- `DataPacker/Source/ExportJobs/ExportTexture.cpp` — `Handles` extension set (`:28`), `VkFormat` selection
  (`:59-66`), and the `bRawTexture` raw-route gate (`:85`). These three must be reconciled.
- `DataPacker/Source/ExportJobs/CLAUDE.md` — "Handles() and ChunkFlags"; update if the supported-format set
  changes (standard doc step).

## Notes

- Offline DataPacker only — no runtime, CRC, or determinism exposure; the worst live outcome is a bad/missing
  bake, surfaced at next engine load.
- One grill decision: dormant (drop from `Handles`) vs latent bug (wire into raw route), resolved by an asset
  -tree search for the three extensions; plus the parallel `.R32_SFLOAT` reconciliation.
- This is the sibling of the report's earlier "Removed unreachable R32_SFLOAT raw route" doc note — the
  exporter's claimed-vs-actual format support has drifted and needs a single consistent set.
