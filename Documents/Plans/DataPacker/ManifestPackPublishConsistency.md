<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-18T01:11:40.000Z","dependsOn":[]} -->
# Detect a manifest published over a mismatched pack

## Context

`RunExportJobs<T>` in `DataPacker/Source/Main.cpp` (lines 81-298) publishes a successful export by renaming three temporary files in sequence at the end of the success branch: the generated CRC header (conditionally, lines 284-291), then the manifest (line 293), then the pack (line 294). The manifest and pack renames are the final two statements of the success branch and are not atomic with respect to each other.

A process kill *between* those two renames, on a run whose only change was a **deleted source asset**, publishes a new N-chunk manifest over the still-old (N+1)-chunk pack. No later run detects the mismatch — every existing dirty check passes:

- The manifest chunk-count comparison (`Main.cpp:131`, `bDirty |= iManifestChunkCount != static_cast<int64_t>(exportJobs.size());`) reads the *new* manifest's N and compares it against the N live jobs — it passes.
- A deletion touches no `.meta` fingerprint, so the pack-older-than-fingerprint check added by 489ecd92 (`Main.cpp:138-153`) compares the old pack's mtime against untouched, older fingerprints — it passes.
- Every remaining job's chunk cache is clean, so `ExportJob::CheckDirty` (`DataPacker/Source/ExportJobs/ExportJob.h:22`) passes.

The aggregate is therefore judged clean and the export is skipped. The runtime then reads the new manifest's `common::ChunkLocation` offsets/sizes against old pack bytes — every chunk at or after the deleted asset's former offset resolves to wrong bytes. This is a data-corruption path reaching runtime chunk reads, not merely a stale generated constant.

This is the sibling of the header stale window closed by reordering the header rename ahead of the manifest/pack renames (the comment at `Main.cpp:275-283` documents that ordering). That reorder is landed and is not a live plan; it fixed the header/pack pair and does not address the manifest/pack pair.

Evidence origin: independently confirmed twice in one session — by a `/plan-audit` of the header-window plan and by the session's `/repo-code-review`. The finding is out of scope for that change, which owned only the header ordering.

## Design

Verify-first, then one fixed fix. The implementer executes the three steps below in order; there are no open design choices — if verification in step 1 or step 3 fails, the outcome is stop-and-surface, never an implementer-chosen alternative.

**Why no rename reorder fixes this.** Swapping to pack-before-manifest merely moves the hole: a kill in the window would then publish a new pack under an old manifest, which misses the *changed-asset* case symmetrically. The pair needs an actual consistency check, not an ordering.

**Step 1 — reproduce the failure mode against current source before editing.** Construct the equivalent on-disk state directly (per `DataPacker/Source/AGENTS.md`: reconstruct target state from known-good inputs rather than triggering a full re-export): new N-chunk manifest beside the old (N+1)-chunk pack, clean fingerprints, clean chunk caches. Confirm the current dirty checks judge it clean and skip the export. If current code already detects it, stop and surface — the plan's premise has drifted.

**Step 2 — the fix: CRC-set comparison in the dirty check.** Compare the published manifest's chunk CRC set against the live job CRC set during the dirty check:

- The manifest stores one `common::ChunkLocation` per chunk (`Common/DataFile.h:42-53`) carrying a path-derived `crc` field, and each live job carries `mCrc = Crc(mRelativeFile)` (`ExportJob.h:41`).
- The existing manifest-header read (`Main.cpp:92-100`) already opens the manifest and consumes `common::DataHeader`; extend that same read to also consume the `iChunkCount` `ChunkLocation` entries that follow it, collecting their `crc` values into a local container (workbuffer rules do not apply — DataPacker is untracked). A short read or stream failure dirties the aggregate, matching the existing header-read failure handling on line 98.
- Replace the count-only comparison at `Main.cpp:131` (and its explanatory comment, lines 126-130) with an order-insensitive equality test between the manifest CRC set and the live jobs' `mCrc` set, evaluated at the same point (after job discovery, lines 112-124). A count check is a weaker form of the same test, so this subsumes it; preserve the existing moot-when-already-dirty behavior (the current code's `iManifestChunkCount == -1` sentinel case).

**Step 3 — verify coverage and cost before finishing.** Confirm the new check dirties the step-1 state, that a fully clean warm-cache run still skips sub-second, and that the two checks together (CRC set + 489ecd92 fingerprint-newer-than-pack) cover both kill windows. Note the window is not closed for a *changed*-asset kill by CRC comparison alone — CRCs are path-derived, so a same-path content change produces an identical CRC set. That case is already covered by the 489ecd92 check, since a changed asset re-commits its `.meta`. Any residual window the change cannot close is reported, never left silent; if verification refutes coverage or the warm-cache cost regresses, stop and surface rather than redesigning.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change satisfying the acceptance criteria, and add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants no permission to touch anything in it beyond the named regions plus the mechanical necessities (includes, local declarations) the named change requires.

**In scope** — `DataPacker/Source/Main.cpp`, inside `RunExportJobs<T>` only:

- The manifest-header read block (`Main.cpp:91-100`): extend to read the trailing `ChunkLocation` entries and collect their `crc` values.
- The chunk-count comparison and its comment (`Main.cpp:126-131`): replace with the CRC-set equality test.

**Out of scope** — everything else, explicitly including:

- All other regions of `RunExportJobs<T>`: the pack/header existence checks, job discovery and `CheckDirty` loop, the fingerprint-newer-than-pack check (lines 138-153), `EnsureLocal`, sorting, duplicate detection, job dispatch, temporary-file writing, and the entire publish/rename sequence (lines 271-295). In particular, **the generated-header rename ordering** is landed — do not revisit it.
- `ExportJob.h`/`.cpp` and the per-job cache design — **fingerprint and chunk-cache design** is settled by 47199a36 and 489ecd92; consume the existing `.meta` fingerprints and chunk cache, do not redesign them.
- `Common/DataFile.h` — **`common::DataHeader::kiVersion` and the `.pack`/manifest on-disk layout** are read-only inputs; the fix reads existing published bytes. If the implementation appears to require a format change, stop and surface it rather than bumping the version.
- Duplicate-asset detection, RDO/Gaea/texture export paths, and the `FileManager` output-materialization contract.

## Critical files

- `DataPacker/Source/Main.cpp` — `RunExportJobs<T>` (lines 81-298) owns the dirty checks (lines 84-153) and the header/manifest/pack rename publish sequence (lines 271-295). The two in-scope regions are inside it.
- `Common/DataFile.h` — `common::ChunkLocation` (lines 42-53: `crc`, `uiOffset`, `uiSize`, `contentCrc`) and `common::DataHeader` (line 417+: `iMagic`, `iVersion`, `iChunkCount`); read-only reference for the manifest layout.
- `DataPacker/Source/ExportJobs/ExportJob.h` — `ExportJob::CheckDirty` (line 22) and `ExportJob::mCrc` (line 41); read-only reference for the live-job CRC.

## Risk tier

Tier 2 — scoped tool behavior. The change is confined to the offline DataPacker dirty check, which runs on the main thread before job dispatch: no simulation determinism/CRC path, wire protocol, serialization-format, threading, trust-boundary, or client/server guard exposure. The one boundary to respect: the manifest bytes being read are a file input, so short reads and stream failures must dirty rather than crash (the existing header read already models this). Note the *defect's* blast radius is runtime data corruption even though the *fix's* is not.

## Acceptance criteria

- A run whose only change is a deleted source asset, killed between the manifest and pack renames, is detected as dirty on the next run and republishes the pack — verified by directly constructing the equivalent on-disk state (new N-chunk manifest beside old N+1-chunk pack, clean fingerprints, clean chunk caches) and observing the next run export rather than skip.
- A fully clean warm-cache run still skips the export and stays sub-second; the added check does not force re-export or re-encode.
- A changed-asset kill window remains covered (the 489ecd92 check still fires); any window the change cannot close is reported rather than left silent.
- DataPacker compiles; a normal export run produces byte-identical `.pack`/`.manifest`/generated-header output for unchanged inputs.

## Notes

- Reads existing published `.pack`/`.manifest` bytes; expected to require no `kiVersion` bump or layout change (see Scope contract).
- Tools builds have no allocation tracker, so the engine LOG float-format-spec restriction does not apply.
- Per `DataPacker/Source/AGENTS.md`: do not verify by triggering a full re-export when only cache/metadata state must change — reconstruct the target on-disk state from known-good inputs and verify with a warm-cache run.
