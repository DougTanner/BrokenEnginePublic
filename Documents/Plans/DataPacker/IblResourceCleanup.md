<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-06T22:03:21.209Z","dependsOn":[]} -->
# Scope-own cmft and OpenCL cleanup in the IBL pre-pass

## Context

Verified by `/external-deep-analysis` over `DataPacker/Source` (baseline `3cb5e9a6`): in `DataPacker/Source/ExportJobs/ExportCubemapIbl.cpp`, cmft images acquired at `:170-180`, `:302-307`, and `:384-396` are released only on the normal tails (`:205-207`, `:312-313`, `:401-406`), and the OpenCL context initialized at `:421-425` is destroyed/unloaded only at `:430-431`. An exception from conversion, filtering, writing, or metadata completion bypasses `imageUnload`/`clDestroy`/`clUnload`. `Documents/C++StyleGuide.txt` rule 5 requires RAII for acquired resources. Scale of the leak: full-resolution half-float cubemap faces plus a GPU context held during stack unwinding. Reviewer caveat: `Main.cpp:513-520` calls IBL generation inline, so an escaping exception stops later exports in that run — the leak matters during unwinding and reporting, not long afterward.

## Design

Guard every successfully acquired cmft image — including each face inside the loading loop — and the OpenCL load/context lifetime with scope-owned cleanup installed immediately after acquisition, preserving the existing release order on the success path. Cleanup does not participate in filtering or serialization, so successful intermediates remain byte-identical.

## Critical files

- `DataPacker/Source/ExportJobs/ExportCubemapIbl.cpp`

## In scope

- Scope-owned cleanup for the cmft image and OpenCL acquisitions listed above; nothing else in the pre-pass.

## Out of scope

- Convolution/filter settings, output formats, fingerprinting, and stale-output reconciliation (owned by the IBL reconciliation plan).

## Risk tier and invariants

Expected Change Workflow Tier 2. Trigger: scoped tool behavior; success path unchanged. Invariants: successful intermediate bytes identical; release order on success unchanged; cleanup runs exactly once per acquisition on failure.

## Acceptance criteria

- An injected post-acquisition failure in each guarded region runs each cleanup exactly once (no double-release, no leak) and the run reports the failure as today.
- Successful pre-pass outputs are byte-identical.

## Notes

Residual carried from analysis: confirm the exact cmft ownership and valid-cleanup semantics for `imageCreate`, `imageLoadStb`, `imageCubemapFromFaceList`, `image*Filter`, `clLoad`, and `clInit` from the vendored `ThirdParty` cmft source during implementation before choosing guard placement.
