<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-06T22:03:15.813Z","dependsOn":["Documents/Plans/DataPacker/MainOrchestrationDecomposition.md"]} -->
# Reconcile stale files out of the generated Attribution tree

## Context

Verified by `/external-deep-analysis` over `DataPacker/Source` (baseline `3cb5e9a6`): `CopyThirdPartyLicenses` (`DataPacker/Source/Attribution.cpp:24-156`) only adds missing or newer copies (`:36-42`) and only creates/copies (`:140-155`). No path removes destinations for a deleted ThirdParty library, a renamed license file, or a fallback file superseded by a primary license, so the generated `Attribution/` tree accumulates obsolete files indefinitely. `DataPacker/Source/AGENTS.md` assigns this producer the generation of the sibling `Attribution/` tree and requires change detection before materialization.

## Design

Build the desired relative-file inventory during the existing read-only discovery/selection pass (which already validates, sorts, and selects primary or fallback files at `:44-138`), include stale-file and stale-directory removals in the pre-materialization change detection so a stale-only difference also counts as dirty, and after `EnsureLocal` reconcile only the recognized Attribution root: copy the pending set and remove destinations not in the desired inventory. Identical inputs produce an identical desired inventory and no writes. The Attribution tree is separate from Data `.pack`/`.manifest` output, so reconciliation cannot alter those bytes.

This lands after `Documents/Plans/DataPacker/MainOrchestrationDecomposition.md`, whose decomposition of the same function (discovery/selection returning the ordered `PendingCopy` sequence, clean gate, publication) provides the seams this change extends.

## Critical files

- `DataPacker/Source/Attribution.cpp`

## In scope

- `CopyThirdPartyLicenses`: desired-inventory construction, stale-aware change detection, and post-`EnsureLocal` reconciliation of the recognized Attribution root only, including removal of empty stale directories.

## Out of scope

- License selection precedence, file contents, timestamps, and copy ordering.
- Any path outside the recognized Attribution output root; `FileManager` materialization mechanics.
- Data `.pack`/`.manifest` output and version constants.

## Risk tier and invariants

Expected Change Workflow Tier 2. Trigger: scoped tool output behavior; the Attribution tree is not runtime-loaded `.pack` data. Invariants: current selected files remain byte-identical; unchanged inputs cause no writes; removals are confined to the recognized Attribution root; low-disk cancellation behavior of materialization is preserved.

## Acceptance criteria

- After removing a ThirdParty library, renaming its license, or adding a primary license over a previously used fallback, the next run's Attribution tree contains exactly the currently selected files, with the obsolete destinations removed.
- With unchanged inputs, the run performs no Attribution writes and existing files remain byte-identical.
