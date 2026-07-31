# `DataPacker/Source`

Asset preprocessing tool that converts raw textures, models, shaders, and audio into optimized binary formats for runtime loading.

The vcxproj provisions ThirdParty before linking and requires the prebuilt configuration library. Linked worktrees consume validated stable primary submodule trees and prebuilt Output; failures never trigger a nested ThirdParty build.

## Architecture

`Main.cpp` orchestrates legacy-intermediate migration, Scene and Island pre-export (including Gaea baking), IBL cubemap convolution, remaining asset exports, generated headers, and ThirdParty attribution collection. Every asset type runs even after an earlier failure. Each type's job failures become one structured aggregate diagnostic, and failed temporary outputs are discarded without replacing prior outputs.

The Gaea bake is separate from `ExportJob`: version-specific archetype handling stays in `ExportJobs/Island/GaeaArchetype.{h,cpp}`, while route orchestration, region processing, and shoreline subdivision remain version-agnostic. Each route patches a temporary archetype under `%LOCALAPPDATA%/BrokenEngine/DataPackerCache/<project>/Gaea/Islands/...`; source `.terrain` files are never changed. Cache ownership and island chunk consumption are documented in `ExportJobs/AGENTS.md`.

Island elevation is meters-absolute. Archetype Sea level and `Island.json` elevation scale place the beach at engine Z 0; DataPacker validates the resulting sea floor against `common::kfSeaBottomMeters`. Auto-crop removes only deepest-water pixels and retains the landmass plus shallow-water coastline halo. The runtime applies no per-island height or water-depth multiplier.

The named Win32 single-instance mutex makes a second process wait for the first. Under a debugger, top-level exceptions remain unwrapped so they break at the throw site. Otherwise, the shared diagnostic reporter owns export aggregates, the prompts and errors raised while writing the outputs to their real location on disk, and top-level exception presentation. It LOGs every record before any UI. Directly reported failures unwind with the explicit already-reported exception type so the top-level catch does not emit the same diagnostic again.

Diagnostic mode defaults to interactive and changes monotonically to noninteractive only after `FileManager` proves a linked worktree through Git common-directory agreement and the expected output path. Missing, malformed, ambiguous, or early identity information therefore fails closed to the existing interactive system-modal behavior. A validated linked-worktree run never opens a diagnostic MessageBox: OK records are acknowledged, while OK/Cancel warnings force Cancel before materialization or output changes.

Three RDO diagnostic CLI modes (`--rdo-sweep`, `--rdo-sweep-full`, `--rdo-sweep-validate`) short-circuit the normal run to profile BC7 encoder settings. Set `BT_DATAPACKER_FORBID_EXPENSIVE_EXPORT=1` for verification runs that must stop before Gaea or texture export/encoding. `BT_DATAPACKER_FORBID_GAEA_EXPORT=1` independently blocks only immediately before a Gaea process launch; agent-driven Local generation keeps this guard set unless an explicit user-approved plan or acceptance criterion requires regenerating Gaea output. Changed island inputs or DataPacker code never authorize the multi-hour Gaea export by themselves.

`FileManager` (`gpFileManager`) owns input/output/cache paths, ThirdParty resolution, CLI parsing, and worktree output copy-on-write. An eligible linked worktree reads validated primary-checkout ThirdParty and generated Data/Attribution symlinks. Unexpected reparse points fail closed. Writers must call `EnsureLocal` after detecting a required change and before opening an output handle; materialization validates and copies into a sibling staging directory before replacing only the recognized symlink. Low-disk cancellation preserves the symlink. `%LOCALAPPDATA%/BrokenEngine/DataPackerCache/<project>` is shared across worktrees for chunks, fingerprints, and the mutable Gaea cache.

Input fingerprints are hex SHA-256 content hashes; directory fingerprints combine sorted relative child paths. Gaea `BakeFingerprint` hashes `Island.json` and its resolved `.terrain` in a line-ending-stable text mode that normalizes CRLF, lone LF, and lone CR to CRLF while preserving every other byte, including a BOM and final-newline presence. The fingerprint-cache identity includes the hash mode, so stable-text and raw hashes never alias. A persistent snapshot-validated fingerprint cache (`%LOCALAPPDATA%/BrokenEngine/DataPackerCache/<project>/InputFingerprints.cache`) keeps warm runs sub-second by rehashing only changed files; large shared-cache files additionally use adjacent fingerprint metadata so unchanged snapshots avoid rehashing.

When a DataPacker change alters only fingerprint or cache-metadata formats (not exported bytes), do not verify by letting the format mismatch trigger a full re-export: the existing chunks, packs, and Gaea caches are known-good. Manually reconstruct the final state instead — write the new-format metadata (job `.meta` fingerprints, `.fingerprint.meta` sidecars, shader `.deps.meta`) derived from the current known-good inputs/caches — then verify with a warm-cache run (no exports, sub-second). CLI accepts zero arguments, deriving sandbox paths from executable layout, or exactly three paths: engine data, project data, and output. `--materialize-data <engine-data> <project-data> <output-data>` is the exact-arity data-only copy-on-write entry point: it materializes only the recognized Data root and runs no exports or Attribution work. `.agents/scripts/Test-DataPackerMaterializeData.ps1` owns its executable fixture coverage and never builds DataPacker. ThirdParty license collection detects changes before materializing Attribution.

Worktree builds have Shared and Local data modes. Shared consumes complete primary-checkout generated output. Local is mandatory for DataPacker source, asset input, generated-header, compression, chunk-layout, or pack/manifest contract changes. Linked worktrees default `RunDataPacker=false`; `/compile` invokes the data-only materializer before an authorized same-baseline Local generation, then the normal producer materializes only dirty output roots before changing them.

`Texture` loads EXR, PNG, and raw inputs, mip-generates, and emits BC4/5/7 or supported uncompressed intermediates. Whole encode chains serialize behind `sEncodeMutex` to bound peak memory. Shared helpers in `Texture.h` own compression, cubemap tagging, intermediate suffixes, and tolerant intermediate-header parsing.

Tools builds have no allocation tracker, so the engine LOG float-format-spec restriction does not apply here. `Pch.h` log categories default to `kVerbose`.

## Design Patterns

- Export processing: `RunExportJobs<T>()` performs content-based dirty checks and launches one `std::async` task per asset. Case-folded relative paths must be unique before deterministic sorting and aggregate assembly. A published pack older than any job's `.meta` fingerprint dirties the whole type, so an export killed between fingerprint commit and pack rename republishes from cached chunks on the next run.
- Atomic output: Jobs write temporary files and rename only on complete success; generated headers update only when content differs.
- Reproducibility: Chunk ordering is machine-independent, but BC7 RDO, OpenCL IBL convolution, and GPU-accelerated Gaea bytes assume one authoritative bake host.

## Output Structure

Each asset type produces a `.manifest`, `.pack`, and generated CRC header. Manifest entries keep the path CRC separate from a content CRC computed offline over the exact emitted chunk bytes before the pack write. DataPacker also generates `DataTypes.h`, aggregate `Data.h`, and the sibling `Attribution/` tree.

## See Also

- `ExportJobs/AGENTS.md` - Asset processors and chunk/cache rules
