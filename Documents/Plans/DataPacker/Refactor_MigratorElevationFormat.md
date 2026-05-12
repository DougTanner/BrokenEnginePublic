# Refactor: Migrate-or-Skip Legacy Elevation Intermediates

Source: post-audit follow-up from the island bake/export pipeline refactor.

## Context

`MigrateLegacyIntermediates` in `Main.cpp` handles legacy raw `.BC[457]_UNORM_BLOCK` / `.R16_UNORM` intermediates in-place by zlib-wrapping or re-encoding them. The island elevation pipeline has since changed: bakes now produce `Elevation.R32_SFLOAT` in meters-absolute units (see `DataPacker/Source/CLAUDE.md`'s elevation-pipeline-is-meters-absolute paragraph). A developer with a pre-rewrite tree on disk will still have an `Elevation.R16_UNORM` chunk sitting next to the per-island intermediates directory. The migrator picks it up, zlib-wraps it, and leaves it on disk — but the new pipeline expects `Elevation.R32_SFLOAT` per `kpcIslandElevation`, so the migrated `.R16_UNORM` file is orphaned. Not actively harmful (no asset corruption), but it wastes migrator wall-time and confuses anyone inspecting the intermediates directory.

A second narrower case: the bake phase's dirty check triggers on `Island.json` / `.terrain` mtime changes, but a stale in-process `Intermediates/Elevation.r32` from the pre-meters era (when the float was normalized [0,1] before the meters conversion landed) wouldn't be caught by that check. In practice the schema-change throws from the legacy `mips` key already force a re-bake today, so this is mostly theoretical — but worth a versioned hash check on bake outputs so future format changes don't depend on a separate schema break to invalidate intermediates.

## Background

- `DataPacker/Source/Main.cpp:140` — `MigrateLegacyIntermediate(const std::filesystem::path& rPath)` — entry point per file. Reads `VkFormat` from extension via `IntermediateFormatFromExtension`; the `VK_FORMAT_R16_UNORM` branch at lines 213-239 zlib-wraps + rewrites with magic.
- `DataPacker/Source/Main.cpp:256-268` — `MigrateLegacyIntermediates` walks all input directories. Called once at startup from `main` (line 679).
- `DataPacker/Source/BakeIslandIntermediates.cpp` — `kpcIslandElevation` (and friends in `kpcIntermediateFiles`) define the *current* set of intermediate filenames the bake phase produces; `Elevation.R32_SFLOAT` is one of them. Any file matching the legacy `Elevation.R16_UNORM` name lives in a per-island intermediates directory.
- `DataPacker/Source/CLAUDE.md` — elevation-pipeline paragraph documents the meters-absolute end-to-end design.

## Proposed Approach

1. In `MigrateLegacyIntermediate`, before the per-format branches, detect `Elevation.R16_UNORM` filenames and short-circuit:
   - Match against the stem (`rPath.stem() == "Elevation"`) and the legacy `.R16_UNORM` extension.
   - One-line LOG at `kInfo` describing the orphan.
   - Choose one of:
     - **(a)** Skip (return). Leaves the file on disk; developer may delete manually.
     - **(b)** Delete the file outright via `std::filesystem::remove`. Cleaner; matches the bake phase's understanding that this file is dead.
   - Recommend (b) — the file is unambiguously orphaned (no live code path reads `Elevation.R16_UNORM`) and a stray file in intermediates risks confusion later.
2. Defer the bake-output versioning to a separate plan if it becomes a real failure. The current schema-change-throws-on-`mips`-key behavior is sufficient gating in practice, and adding a version field to every bake output is a bigger surface than this plan warrants.

Name interfaces: change site is the early-return block of `MigrateLegacyIntermediate` in `Main.cpp:140`. The detect-and-delete sits before the existing `IntermediateFormatFromExtension` check (or immediately after, with a special-case branch — pick whichever reads better).

## Out of scope

- Generalized version field on intermediate magic header — speculative until another format change actually requires it; the existing `kiTextureIntermediateMagic` plus filename-format conventions are sufficient today.
- Migrating any *other* orphaned legacy filename — only `Elevation.R16_UNORM` is known dead. Other formats either still exist or were never written to disk for islands.
- Touching the bake phase's dirty-check logic — that already catches the relevant cases via `Island.json` / `.terrain` mtime.
- Adding a CLI flag to disable the cleanup — always on; it's pure deletion of unambiguously dead files.

## Acceptance criteria

- A tree containing `Elevation.R16_UNORM` files at any depth under an input directory has those files removed (or skipped, per choice in implementation) on the next DataPacker run, with one log line per file.
- No other migrator behavior changes (`.BC[457]_UNORM_BLOCK` re-encode and the `.R16_UNORM` legacy zlib-wrap for non-elevation R16 textures both still work).
- `MigrateLegacyIntermediates` walk time is not noticeably worse on a clean tree (the new check is a string-compare per file; cheap).

## Notes

The bake step writes `Intermediates/Elevation.r32` (lowercase) as its in-process float-raw output before it becomes the final `Elevation.R32_SFLOAT` chunk. The legacy filename in question is the *final-chunk* form `Elevation.R16_UNORM`, which is what the migrator walks. Don't conflate the two filenames during implementation.

The cleanup is single-pass and idempotent — after the first run on a legacy tree, subsequent runs find no `Elevation.R16_UNORM` files and do nothing.
