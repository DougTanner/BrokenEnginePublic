<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-18T01:11:40.000Z","dependsOn":[]} -->
# Detect a manifest published over a mismatched pack

## Context

`RunExportJobs<T>` in `DataPacker/Source/Main.cpp` publishes a successful export by renaming three temporary files in sequence: the generated CRC header, then the manifest, then the pack (`Main.cpp` ~lines 283-293). The manifest and pack renames are the final two statements of the success branch and are not atomic with respect to each other.

A process kill *between* those two renames, on a run whose only change was a **deleted source asset**, publishes a new N-chunk manifest over the still-old (N+1)-chunk pack. No later run detects the mismatch — every existing dirty check passes:

- The manifest chunk-count comparison (`Main.cpp` ~line 131, `bDirty |= iManifestChunkCount != static_cast<int64_t>(exportJobs.size())`) reads the *new* manifest's N and compares it against the N live jobs — it passes.
- A deletion touches no `.meta` fingerprint, so the pack-older-than-fingerprint check added by 489ecd92 (`Main.cpp` ~lines 138-153) compares the old pack's mtime against untouched, older fingerprints — it passes.
- Every remaining job's chunk cache is clean, so `ExportJob::CheckDirty` (`DataPacker/Source/ExportJobs/ExportJob.h:22`) passes.

The aggregate is therefore judged clean and the export is skipped. The runtime then reads the new manifest's `common::ChunkLocation` offsets/sizes against old pack bytes — every chunk at or after the deleted asset's former offset resolves to wrong bytes. This is a data-corruption path reaching runtime chunk reads, not merely a stale generated constant.

This is the sibling of the header stale window closed by reordering the header rename ahead of the manifest/pack renames. That reorder is landed and is not a live plan; it fixed the header/pack pair and does not address the manifest/pack pair.

Evidence origin: independently confirmed twice in one session — by a Fable `/plan-audit` of the header-window plan and by the session's `/repo-code-review`. The finding is out of scope for that change, which owned only the header ordering.

## Design

Verify-first. Reproduce and confirm the failure mode against current source before designing the fix; the finding is evidence-backed but the fix design is **not** settled.

**Why no rename reorder fixes this.** Swapping to pack-before-manifest merely moves the hole: a kill in the window would then publish a new pack under an old manifest, which misses the *changed-asset* case symmetrically. The pair needs an actual consistency check, not an ordering.

**Leading candidate (not settled design — verify first).** Compare the published manifest's chunk CRC set against the live job CRC set during the dirty check. The manifest already stores one `common::ChunkLocation` per chunk (`Common/DataFile.h:42-47`) carrying a `crc` field, and each job carries `mCrc = Crc(mRelativeFile)`. Reading the existing manifest's chunk entries — the header read at `Main.cpp` ~lines 95-99 already opens the manifest and consumes `common::DataHeader` — and requiring the CRC set to equal the live jobs' CRC set would dirty any manifest describing a different chunk set than the live jobs, subsuming the current count-only comparison (a count check is a weaker form of the same test). The executing agent confirms this actually closes the deleted-asset window, and that it does not regress the warm-cache sub-second run cost, before committing to it.

Note the window is not closed for a *changed*-asset kill by CRC comparison alone — CRCs are path-derived, so a same-path content change produces an identical CRC set. That case is already covered by the 489ecd92 fingerprint-newer-than-pack check, since a changed asset re-commits its `.meta`. The executing agent verifies that the two checks together cover both windows, and reports any residual window it cannot close.

## Critical files

- `RunExportJobs<T>` in `DataPacker/Source/Main.cpp` (~lines 81-297) — owns the dirty checks (~lines 89-153) and the header/manifest/pack rename publish sequence (~lines 283-293).
- `common::ChunkLocation` and `common::DataHeader` in `Common/DataFile.h` (~lines 20-50) — manifest on-disk chunk entry (`crc`, `uiOffset`, `uiSize`, `contentCrc`) and header (`iMagic`, `iVersion`, `iChunkCount`).
- `ExportJob::CheckDirty` and `ExportJob::mCrc` in `DataPacker/Source/ExportJobs/ExportJob.h` — per-job cache dirty check and the path-derived chunk CRC.

## Out of scope

- **The generated-header rename ordering** — landed; the header now publishes ahead of the manifest/pack pair. Do not revisit that ordering.
- **Fingerprint and chunk-cache design** — settled by 47199a36 and 489ecd92. Consume the existing `.meta` fingerprints and chunk cache; do not redesign them.
- **`common::DataHeader::kiVersion` / `.pack` / manifest on-disk layout** — the fix reads existing published bytes. If a candidate design appears to require a format change, stop and surface it rather than bumping the version.
- Duplicate-asset detection, RDO/Gaea/texture export paths, and the `FileManager` output-materialization contract.

## Acceptance criteria

- A run whose only change is a deleted source asset, killed between the manifest and pack renames, is detected as dirty on the next run and republishes the pack — verified by reproducing the kill window (or by directly constructing the equivalent on-disk state: new N-chunk manifest beside old N+1-chunk pack, clean fingerprints, clean chunk caches) and observing the next run export rather than skip.
- A fully clean warm-cache run still skips the export and stays sub-second; the added check does not force re-export or re-encode.
- A changed-asset kill window remains covered (the 489ecd92 check still fires); any window the change cannot close is reported rather than left silent.
- DataPacker compiles; a normal export run produces byte-identical `.pack`/`.manifest`/generated-header output for unchanged inputs.

## Notes

- **Invariant exposure:** offline DataPacker tool only — no simulation, determinism/CRC sim path, wire protocol, threading, or client/server guard exposure. The dirty check runs on the main thread before job dispatch. **However, the failure mode it fixes reaches runtime chunk reads**: a mismatched manifest/pack pair makes the engine resolve valid-looking chunk offsets against wrong bytes, so the defect's blast radius is the shipped data, even though the fix's is not.
- Reads existing published `.pack`/`.manifest` bytes; expected to require no `kiVersion` bump or layout change (see Out of scope).
- Tools builds have no allocation tracker, so the engine LOG float-format-spec restriction does not apply.
- Per `DataPacker/Source/AGENTS.md`: do not verify by triggering a full re-export when only cache/metadata state must change — reconstruct the target on-disk state from known-good inputs and verify with a warm-cache run.
