# DataPacker Export Jobs

Asset processors convert source files into cached `.pack` chunks. The parent `RunExportJobs<T>` orchestration (`../AGENTS.md`) discovers each job by `Handles()` and assembles its output.

## Shared Pipeline

- `ExportJob` owns content-fingerprint dirty checks, cache metadata, chunk I/O, and failure cleanup. `%LOCALAPPDATA%/BrokenEngine/DataPackerCache/<project>` holds versioned chunks and fingerprints; checkout-local packs and manifests can be rebuilt from clean shared chunks without re-exporting sources.
- A successful export writes derived metadata before the primary fingerprint. Interrupted or failed work therefore remains dirty, and jobs remove incomplete sidecars in `CleanupOnFailure()`.
- `ExportJob::Version(N)` folds in `sizeof(common::ChunkHeader)`. Jobs that serialize additional payload structs also fold in their sizes. Same-size reorder or semantic changes need the owning manual version bump.
- A job may version an expensive sub-stage independently of its chunk payload when that stage's outputs are tracked in the checkout, giving the stage its own marker and a fingerprint covering only its own inputs. Keep that version outside `Version(...)` so an unrelated chunk-header change cannot rewrite tracked outputs, and bump whichever version owns the behavior that changed; island texture encoding is the current instance.
- Each job constructs a `common::ThreadLocal` on its worker and uses that thread's workbuffer. Keep job output and scratch isolated from other parallel exports.
- `AllocateHeaderAndData` is normally called once per export. Scene animation data is appended afterward; its chunk size excludes that section and a header pointer captured before vector growth is invalid after reallocation.
- `BT_DATAPACKER_FORBID_EXPENSIVE_EXPORT=1` must fail before dirty Gaea or texture encoding begins. Clean cached outputs remain readable under this guard.

## Matching and Routing

`Handles()` determines both ownership and initial chunk flags. Filename tags and path components are load-bearing inputs: `[C]` marks cubemaps, `[BC4]`/`[BC5]`/`[BC7]` select block formats, and a `Raw` component marks raw assets. Shader stage flags and compression flags are added by their owning jobs. Scene discovery ignores `Intermediates` paths so generated Gaea meshes do not become standalone scenes.

Regular images generate a full mip chain; raw BCn/R16 and half-float intermediates preserve their supplied mip/face layout; KTX and live six-face cubemaps preserve cubemap ordering. Every final texture chunk is LZ4-compressed. Regular-path BC5 textures also publish the per-mip slope-variance data consumed by water shading; raw and cubemap paths do not synthesize it.

Texture encoding and chunk routing are described in `Texture/AGENTS.md`. Island/Gaea ingest is described in `Island/AGENTS.md`.

## Scene and Model

Scene export is two-phase. A versioned `.PreExport` marker governs generation of model and block-compressed texture intermediates; the main phase writes scene metadata and optional animation data. Pre-export also removes orphaned scene texture intermediates so recursive texture discovery cannot ship stale assets. Generated assets are referenced by relative-path CRC.

When multiple mesh nodes reuse one glTF material, preserve distinct material entries while retaining the source material index used for texture lookup.

## Cubemap Pre-pass

Irradiance and prefiltered cubemaps are generated before ordinary export jobs. Their cache fingerprints cover the source KTX or all six faces, and incomplete writes remain dirty. Prefiltered radiance is face-major/mip-minor to match runtime upload order. The resulting half-float intermediates are consumed through texture raw routing.

## Shader Dependencies

Shader export records every dependency from `glslc -MD` as an input-root index, root-relative path, and content fingerprint. Never persist absolute worktree paths. Missing or changed dependencies recompile the shader; Vulkan SDK header-version changes also invalidate shader chunks.

## Audio Policy

Audio export produces interleaved 48 kHz PCM for the runtime mastering rate. Repairs operate only on packed output and run before resampling; source files are never modified. `_loop` filename stems use seam validation instead of edge fades, and a `Music` path component limits repair to safe transformations. Policy, repair-order, or export-rate changes require an `ExportAudio` version bump.

## See Also

- `Island/AGENTS.md` - Gaea route caches, split lifecycle, and island payloads
- `Texture/AGENTS.md` - texture intermediates, RDO, migration, and chunk formats
- `../../../Common/AGENTS.md` - shared chunk and serialization contracts
