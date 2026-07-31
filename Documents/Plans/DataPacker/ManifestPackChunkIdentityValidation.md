<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-26T12:31:54.834Z","dependsOn":[]} -->
# Validate published manifest entries against pack chunk identities

## Context

`RunExportJobs<T>` in `DataPacker/Source/Main.cpp` publishes a completed export by renaming the generated header, manifest, and pack in sequence. A process kill after the manifest rename but before the pack rename can leave a new manifest beside the old pack. When the triggering change was a deleted source asset, the new manifest and current live jobs both contain N paths while the old pack still contains N+1 chunks, so the existing manifest-count and fingerprint-newer-than-pack dirty checks both pass.

The rejected earlier Plan proposed comparing manifest path CRCs with live-job path CRCs. Plan audit proved that comparison cannot observe this failure: both collections come from the same N current jobs, while the stale identity exists only in the old pack. The published manifest must instead be validated against the pack bytes it addresses.

The existing format already carries the needed identity. Each manifest `common::ChunkLocation` records a path-derived `crc`, offset, and size, while every packed chunk begins with a `common::ChunkHeader` carrying the same path-derived `crc`. The manifest table begins at the 16-byte-aligned offset after `common::DataHeader`, not immediately after the 24-byte header.

## Design

Make one bounded manifest-to-pack consistency check inside the pre-export dirty path of `RunExportJobs<T>`.

1. Extend the existing manifest-header read to load its `ChunkLocation` table safely. Measure manifest size, compute `RoundUp(sizeof(common::DataHeader), common::kiAlignmentBytes)`, and require `0 <= iChunkCount <= iMaxChunks` before sizing a local vector or multiplying a byte count. Seek to the aligned table offset and read exactly the declared entries. Missing, malformed, out-of-range, or truncated input sets the aggregate dirty; it never drives an unbounded allocation or terminates the tool.
2. After the published pack is known to exist, validate the manifest locations against it without hashing payload bytes. Starting at expected pack offset zero, require each location to begin at the expected 16-byte-aligned offset, contain at least one complete `ChunkHeader`, and stay within the actual pack size using overflow-safe range arithmetic. Read the `ChunkHeader` at every location and require stream success, `ChunkHeader::kiMagic`, and `chunkHeader.crc == chunkLocation.crc`. Advance the expected offset by the location size and writer-equivalent alignment; after the last entry, require the expected extent to equal the pack file size. A zero-entry manifest therefore requires a zero-byte pack.
3. Preserve the existing manifest-count versus live-job-count comparison. It remains the source-set deletion detector that starts the original export and retains cardinality before duplicate-path rejection. Preserve the fingerprint-newer-than-pack check for same-path changed assets; manifest-to-pack header identity is deliberately not a payload-integrity replacement.

This combination detects the interrupted deletion publish: the first entry after a removed non-final chunk points at an old pack header with a different path CRC, while removing the final chunk leaves a trailing pack extent that fails the exact-size check. It keeps the existing `.pack` and `.manifest` layout, `DataHeader::kiVersion`, publish ordering, and generated-header behavior unchanged.

## Critical files

- `DataPacker/Source/Main.cpp` — `RunExportJobs<T>` owns the manifest read, pack existence check, aggregate dirty checks, and the only implementation changes.
- `Common/DataFile.h` — read-only definitions of `kiAlignmentBytes`, `ChunkLocation`, `ChunkHeader`, and `DataHeader`; no layout or version changes.
- `DataPacker/Source/ExportJobs/ExportJob.cpp` — read-only confirmation that packed chunk headers receive the same path-derived CRC and that fingerprint metadata covers same-path content changes.
- `Engine/Source/File/PackChunks.cpp` — read-only model for bounded manifest count/table parsing at the runtime trust boundary.

## In scope

- `DataPacker/Source/Main.cpp` — `RunExportJobs<T>`, the existing manifest-header read: extend it to load the manifest `common::ChunkLocation` table safely, measuring manifest size, computing the aligned table offset as `RoundUp(sizeof(common::DataHeader), common::kiAlignmentBytes)`, requiring `0 <= iChunkCount <= iMaxChunks` before sizing a local vector or multiplying a byte count, and reading exactly the declared entries; missing, malformed, out-of-range, or truncated input sets the aggregate dirty.
- `DataPacker/Source/Main.cpp` — `RunExportJobs<T>`, after the published pack is known to exist: add the manifest-to-pack consistency check that walks the locations from expected offset zero, requires each location to start at the expected 16-byte-aligned offset, to contain at least one complete `common::ChunkHeader`, and to stay inside the actual pack size using overflow-safe range arithmetic; reads the `ChunkHeader` at every location and requires stream success, `ChunkHeader::kiMagic`, and `chunkHeader.crc == chunkLocation.crc`; advances the expected offset by the location size plus writer-equivalent alignment; and requires the final expected extent to equal the pack file size.
- `DataPacker/Source/Main.cpp` — `RunExportJobs<T>`, existing aggregate dirty checks: preserve the manifest-count versus live-job-count comparison and the fingerprint-newer-than-pack check unchanged alongside the new check.

## Out of scope

- Changing manifest-before-pack publication order, adding a journal/marker file, or making the two renames atomic.
- Changing `ChunkLocation`, `ChunkHeader`, `DataHeader`, `DataHeader::kiVersion`, or any `.pack`/`.manifest` bytes produced for unchanged inputs.
- Hashing complete pack payloads or promoting `ChunkLocation::contentCrc` into a new runtime or DataPacker integrity policy.
- Changing per-job chunk caches, input fingerprints, `ExportJob::CheckDirty`, duplicate-path detection, exporter implementations, generated-header publication, or `FileManager` materialization.
- Runtime `PackChunks` behavior; its bounded manifest reader is a reference pattern only.
- Refactoring adjacent `RunExportJobs<T>` code or extracting a reusable parser.

## Risk tier

Tier 3 — the change extends validation of opaque manifest and pack file input at a trust boundary. It remains confined to the offline DataPacker main-thread dirty check and changes no simulation determinism, wire protocol, serialization layout, threading, or client/server affinity, but malformed counts, offsets, sizes, and stream failures must all fail dirty without unsafe allocation or arithmetic.

## Acceptance criteria

- Before implementation, directly reconstruct a new N-entry manifest beside its known-good old N+1-chunk pack with clean remaining fingerprints and chunk caches. Confirm the current DataPacker incorrectly skips the aggregate; if it already becomes dirty, stop because the premise drifted.
- After implementation, the same reconstructed state becomes dirty and republishes the pack from cached chunks. Exercise both a deleted middle chunk, which must fail header CRC identity, and a deleted final chunk, which must fail exact pack extent.
- Bounded-input checks mark negative or file-capacity-exceeding manifest counts, a truncated table, an out-of-range or overlapping location, a truncated chunk header, a header magic/CRC mismatch, and short or trailing pack extent dirty without crashing or attempting an allocation derived from an unvalidated count.
- A same-path changed-asset interrupted state with an unchanged path CRC remains detected by the existing fingerprint-newer-than-pack check.
- A fully clean warm-cache run skips export and remains under one second. The check reads only the manifest table and one fixed-size header per chunk; it does not hash or load pack payloads.
- DataPacker compiles. For unchanged inputs, `.pack`, `.manifest`, and generated-header hashes remain byte-identical before and after the verification run.

## Notes

- Per `DataPacker/Source/AGENTS.md`, reconstruct cache and metadata states from known-good inputs rather than triggering an unnecessary full re-export.
- Tools builds have no allocation tracker, but all container sizing remains behind validated manifest bounds because the bytes are opaque file input.
