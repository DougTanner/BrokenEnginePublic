# Generated Header Stale Window

## Context

Commit 489ecd92 closed the stale-pack window in `RunExportJobs<T>` (`DataPacker/Source/Main.cpp:138-153`): when any committed `.meta` fingerprint is newer than the published pack, the whole type dirties and republishes from cached chunks. A narrower window remains in the publish sequence (`Main.cpp:274-285`): the manifest renames (line 274), then the pack (line 275), then the generated CRC header renames only when its content changed (lines 278-281). A process kill after the pack rename but before the header rename leaves a stale header undetected — pack and manifest are new, every fingerprint is older than the fresh pack, and the dirty check only tests header *existence* (lines 107-110), never its staleness. The window only matters when the changed asset set actually altered generated CRC constants that run, and it self-heals on the next real asset change; severity is low.

Findings originate from a second-pass multi-agent review and are unverified claims; the executing agent must confirm each one before editing.

## Design

1. **Verify first** (per Diagnosis Discipline; refuted claims dropped as named residuals). Confirm the rename ordering in `RunExportJobs<T>` and that the fingerprint dirty comparison covers only `packFile`, not `headerFile`. Then determine whether the conditional header rename can simply be ordered before the pack rename: the fingerprint check trusts the pack rename as the publish commit point, so a kill between an early header rename and the pack rename leaves fingerprints newer than the old pack — already dirty, already republished. If that reasoning holds, prefer the reorder; it closes the window with zero new checks.
2. If reordering is unsafe or insufficient (e.g. the header must not precede the manifest/pack for a reason verification uncovers): extend the existing mtime dirty comparison at `Main.cpp:138-153` to the generated header the same way 489ecd92 did for the pack — smallest change, fail-safe direction (filesystem errors dirty).
3. If verification shows the window is already covered or unreachable: record the evidence and close as verified-no-change with a clarifying comment at the check site.

## Critical files

- `DataPacker/Source/Main.cpp` — `RunExportJobs<T>` dirty check (lines 138-153) and publish rename sequence (lines 274-285).

## Out of scope

- Fingerprint/cache design — settled by 47199a36 and 489ecd92.
- Any new hashing, retry, or verification machinery; this is a one-comparison or one-reorder change.
- Other DataPacker work.

## Acceptance criteria

- Every executed design item has a recorded verification result preceding its change.
- A simulated kill between the pack rename and the header rename (or reasoning from the reordered sequence) shows the next run republishes/regenerates the header.
- DataPacker compiles and a clean warm run stays not-dirty.

## Notes

- Invariant exposure: offline DataPacker only. The generated CRC header feeds engine compilation, but the fix changes only staleness detection/ordering, not header content, `.pack` layout, `kiVersion`, runtime determinism/CRC, replay, client/server guards, or allocation-tracked paths.
