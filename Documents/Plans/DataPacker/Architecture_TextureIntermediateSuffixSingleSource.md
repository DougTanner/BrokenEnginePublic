# Architecture: Texture Intermediate Suffix Single Source

## Context
Spun out of the `/next-plan` Step-6 sibling sweep run while executing `DataPacker/Architecture_SceneLoaderVestigialPaths.md` (which deduped the two `VkFormat`→filename-suffix switches inside `ExportScene.cpp` into one `TextureIntermediateSuffix(VkFormat)` free helper in that TU's anonymous namespace). The same `VkFormat`↔intermediate-suffix string coupling (`.BC4_UNORM_BLOCK` / `.BC5_UNORM_BLOCK` / `.BC7_UNORM_BLOCK`, plus `.R16_UNORM` / `.R16G16B16A16_SFLOAT`) is independently hand-maintained at several more producer/consumer sites in the DataPacker texture pipeline. Drift between any pair silently dangles a texture CRC: the scene/island chunk references a CRC computed from one suffix while the emitted intermediate (and the chunk ExportTexture routes it to) uses another → silently-missing texture at runtime, no error.

The engine runtime is unaffected — textures are consumed by CRC and `VkFormat` is read from the chunk header, never re-parsed from the filename (`TextureUploadManager` branches on `VkFormat`, not on the suffix) — so the hazard is entirely producer-side within DataPacker.

User decision (`/next-plan` Step 9, on the SceneLoaderVestigialPaths run): these sibling sites were **deferred to this follow-up plan** rather than folded into the SceneLoaderVestigialPaths change.

## Design
Promote the `ExportScene.cpp` `TextureIntermediateSuffix(VkFormat)` helper (from the SceneLoaderVestigialPaths landing) to a single canonical `VkFormat`↔suffix source and route every other site through it.

### Sites (all under `DataPacker/Source/ExportJobs/`)
- **`ExportScene.cpp`** — already has the canonical forward mapping `TextureIntermediateSuffix(VkFormat)` (anonymous-namespace free function, used by `GetTextureIntermediatePath` and the `MainExport` relative-path/CRC build). Hoist it to a shared home so the sites below can include it.
- **`Texture/MigrateLegacyIntermediates.cpp` (`IntermediateFormatFromExtension`, ~`:9-13`)** — the **inverse** mapping (suffix string → `VkFormat`). Derive from the same canonical table so forward and inverse cannot diverge; a missed suffix here silently skips a legacy intermediate from migration (wrong/`UNDEFINED` format).
- **`ExportTexture.cpp` (`extensionSet` claim list `~:15` + the `find(L".BCn_UNORM_BLOCK")` routing chain / `bRawTexture` test `~:38-59`)** — the consumer end of the CRC chain (turns the writer's suffix into the emitted chunk's `VkFormat`). The scene CRC at `ExportScene.cpp` dangles iff writer and this consumer disagree. **Coordinate with `DataPacker/Refactor_ExportTextureMechanics.md` if it is still queued** — it independently reworks this exact path-sniffing into a one-pass `extension()`/`filename()` resolve, so land that first or co-schedule and fold the canonical-suffix lookup into its rewrite (don't double-edit `:38-59`). If it has already landed, re-read the post-rework resolve and route it through the canonical table instead.
- **`ExportIsland.h` constants (`~:12-15`: `kpcIslandAmbientOcclusion`/`kpcIslandColor`/`kpcIslandMasks`/`kpcIslandNormals`)** paired with the `VK_FORMAT_BCn` `Save` calls (`ExportIsland.cpp:215-306`) and CRC builds (`:414-428`): each constant's suffix must agree with the adjacent format arg or the island-header CRC (`ambientOcclusionCrc`/`colorsCrc`/`normalsCrc`/`masksCrc`) dangles. Derive the constants from the canonical source (or assert agreement). Medium-confidence sibling — the island side already centralizes the string in a constant, so its internal risk is the constant-vs-format pairing, not string duplication.

### Canonical home (one open design decision — pre-staged for `/external-grill-plan`)
Where the shared `VkFormat`↔suffix table lives — a new `DataPacker/Source/ExportJobs/Texture/` header, an existing shared DataPacker header, or `common::`/`DataFile.h` if the inverse is ever wanted engine-side. Pick the smallest home all four DataPacker TUs can include without dragging extra dependencies.

## Critical files
- `DataPacker/Source/ExportJobs/ExportScene.cpp` (canonical helper home)
- `DataPacker/Source/ExportJobs/Texture/MigrateLegacyIntermediates.cpp`
- `DataPacker/Source/ExportJobs/ExportTexture.cpp`
- `DataPacker/Source/ExportJobs/ExportIsland.{h,cpp}`

## Out of scope
- The `ExportScene.cpp` two-switch unification itself — landed by `Architecture_SceneLoaderVestigialPaths.md`.
- Any change to the suffix strings or the on-disk intermediate format (pure centralization; bytes/paths/CRCs identical).
- Engine-side texture loading (consumes by CRC + chunk-header `VkFormat`; no filename parsing).

## Acceptance criteria
- Exactly one `VkFormat`→suffix definition (and one inverse) in the DataPacker tree; the sibling sites reference it.
- `.pack`/`.manifest`/chunk bytes and all texture CRCs identical before/after (full bake + game-load smoke check).

## Notes
- Offline DataPacker tool only — no runtime CRC/determinism, no `kiVersion`/`.pack`-layout change, no replay/client-server exposure. Output bytes and CRCs identical (centralization only).
- Builds on `Architecture_SceneLoaderVestigialPaths.md` (Pattern-3 helper) — land after it.
- Coordinate the `ExportTexture.cpp:38-59` edit with `DataPacker/Refactor_ExportTextureMechanics.md` (if still queued) to avoid double-editing the path-sniffing block; if it has landed, route its one-pass resolve through the canonical table.
- Citations here are approximate (`~:`) and will be refreshed by `/next-plan` at execution; the concurrent DataPacker dedup sessions move these lines.
- One grill decision pre-staged: the canonical home for the shared table.
