# Architecture: Cross-Machine Pack-Byte Determinism

## Context
Source: /external-architecture-review on `DataPacker/Source/ExportJobs` (recursive). **Decision plan (present options).** `Main.cpp:382` documents stable chunk bytes as a goal ("for more efficient Steam patching"), and chunk ordering is correctly deterministic (sorted by lowercased relative path). But two encode paths produce bytes that vary across machines:

1. **BCn RDO encode** — `Texture::EncodeWithRdo` sets `params.m_rdo_max_threads = HardwareCoreCount() - 2` with `m_rdo_multithreading = true` (`Texture.cpp:384`). bc7enc_rdo's ERT partitions the block stream per thread and matches cannot cross partition boundaries, so encoded bytes vary with core count (stable per machine, different across machines).
2. **IBL convolution** — `GeneratePreFilteredCubemaps` initializes OpenCL on whatever GPU is present (`ExportCubemapIbl.cpp:135-139`) and feeds `pClContext` into `cmft::imageRadianceFilter` (:223, :295); output half-floats depend on GPU/driver. The CPU fallback uses `hardware_concurrency` threads (:133).

If all shipping packs are baked on one machine, this is moot; if bakes can come from different machines (CI, multiple devs), every texture/IBL chunk diffs on every bake host change, defeating the patch-size goal.

## Design

Decision: pick per path —

### RDO thread count (`Texture.cpp:384`)
- **(A) Pin `m_rdo_max_threads` to a fixed constant** (e.g. 8): bytes become machine-independent; encode slows on high-core machines (whole-texture encodes are already serialized behind `sEncodeMutex`, so wall-clock impact is bounded but real) [~5m]
- **(B) Leave as-is and document the single-bake-machine assumption** at the knob site and in `DataPacker/Source/CLAUDE.md` [~5m]

### IBL OpenCL path (`ExportCubemapIbl.cpp:133-139`)
- **(A) Force the CPU path with a pinned thread count**: machine-independent but significantly slower convolution [~10m]
- **(B) Leave as-is and document** that IBL intermediates are GPU-dependent and should be baked on the canonical machine [~5m]

Recommendation: (B)/(B) — document — unless multi-machine baking is actually planned; revisit with (A)/(A) when CI bakes appear.

## Critical files
- `DataPacker/Source/ExportJobs/Texture/Texture.cpp` (`EncodeWithRdo`, `m_rdo_max_threads`)
- `DataPacker/Source/ExportJobs/ExportCubemapIbl.cpp` (`clLoad`/`clInit` + cmft calls)
- `DataPacker/Source/CLAUDE.md` (document whichever assumption is chosen)

## Out of scope
- Chunk ordering — already deterministic (`Main.cpp:383` sort by `common::ToLower(mRelativeFile)`).
- Runtime determinism/replay CRC — pack bytes feed assets, not the simulation contract.
- The `std::sort` case-collision tie (two relative paths differing only in case across input directories) — practically negligible.

## Notes
- Decision plan (present options) — one grill decision per path; (B)/(B) is wording-only with Risks 0, (A) variants change emitted bytes once (one-time full re-export diff) and are trivially reverted.
- Per-machine output is already stable either way; this is exclusively about cross-machine reproducibility.

## Verification Notes
- Verified — no invalid items. The bc7enc_rdo thread-partition claim was confirmed in the library source: `ThirdParty/bc7enc_rdo/rdo_bc_encoder.cpp:671-683` partitions the block stream into `m_rdo_max_threads` contiguous per-thread ranges processed independently, so encoded bytes vary with the thread count. `m_rdo_max_threads = HardwareCoreCount() - 2` at `Texture.cpp:384` (`m_rdo_multithreading = true` at :382).
- IBL citations exact: CPU thread count from `hardware_concurrency` at `ExportCubemapIbl.cpp:133`, OpenCL init :135-139, `pClContext` into `imageRadianceFilter` at :223 and :295. Note `GenerateIrradianceCubemaps` uses `imageIrradianceFilterSh` (CPU SH, no CL context, no thread-count parameter) — correctly excluded from the plan.
- Chunk-ordering determinism confirmed (`Main.cpp:382-383` sort by `ToLower(mRelativeFile)`); whole-texture encodes serialized behind `Texture::sEncodeMutex` confirmed (plus the `sActiveEncodeCount` assert at `Texture.cpp:40/:377`), so option (A)'s wall-clock impact framing is accurate.
