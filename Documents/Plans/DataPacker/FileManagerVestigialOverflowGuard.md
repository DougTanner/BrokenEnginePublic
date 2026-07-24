<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-18T01:11:40.000Z","dependsOn":[]} -->
# FileManager Vestigial Overflow Guard Removal

## Context

`DataPacker/Source/FileManager.cpp` defines a private `AddChecked(uint64_t, uint64_t)` in its anonymous namespace (`FileManager.cpp:179-186`) that throws `std::runtime_error("Output materialization size overflow")` when a `uint64_t` addition would wrap. It has exactly one call site — the cluster-rounded file-size accumulation inside `FileManager::MaterializeOutput`'s output-materialization walk (`FileManager.cpp:445`):

```cpp
uiAllocation = AddChecked(uiAllocation, AddChecked(uiSize, uiClusterBytes - 1) / uiClusterBytes * uiClusterBytes);
```

`uiSize` comes from `directory_entry::file_size()` over the real on-disk output tree, and `uiClusterBytes` from `GetDiskFreeSpaceW` results (`FileManager.cpp:429-433`). Overflowing a `uint64_t` sum of actual file sizes requires roughly 18 exabytes of output, so neither guard has a reachable failure mode. Per the repository's useless-guard/unreachability rule (same family as the no-useless-ASSERTs directive), the guard adds false safety rather than protection.

This is the useless-guard rule, not the trust-boundary rule. The trust-boundary directive governs *obtaining* these values — the existing `GetDiskFreeSpaceW` failure check (`FileManager.cpp:429-432`), the reparse-point rejection (`:436-440`), and the unsupported-entry rejection (`:447-450`) are the real validation and all stay.

Precedent: the identical `AddChecked`/`MultiplyChecked` pair was already deleted from `DataPacker/Source/DiagnosticReporter.cpp`, which deliberately left this second copy for a follow-up because it lives in a different translation unit. That deletion is confirmed — `ReportMaterializationDiskSpace` (`DiagnosticReporter.cpp:118`) now inlines plain arithmetic (`(uiAllocation * 5 + 99) / 100` at `:120`, `uiAllocation + uiReserve` at `:121`) with no helper. This plan closes the same pattern in its remaining translation unit.

## Design

1. **Verify the unreachability claim before editing** (per Diagnosis Discipline). Confirm against current source that `AddChecked` has exactly one call site (`FileManager.cpp:445`, inside `FileManager::MaterializeOutput`), that both its arguments derive from on-disk file sizes and the cluster size rather than from attacker- or config-controlled input, and that no other translation unit reaches this anonymous-namespace definition. If any of this is refuted — for example a second call site appeared, or a caller can drive `uiAllocation` from something other than real file sizes — stop and report rather than deleting.
2. Delete the `AddChecked` definition (`FileManager.cpp:179-186`).
3. Inline plain arithmetic at the sole call site (`:445`), preserving the existing cluster-rounding semantics exactly — round `uiSize` up to the next `uiClusterBytes` multiple with `(uiSize + uiClusterBytes - 1) / uiClusterBytes * uiClusterBytes`, then accumulate into `uiAllocation` with plain `+=`/`+`. Mirror the `DiagnosticReporter.cpp` precedent's shape.
4. Remove any `#include` or `using` that the deletion leaves unused. As of this writing there is nothing to remove: `FileManager.cpp` has no local standard-library includes (`std::numeric_limits` arrives through the PCH via `Common/ExternalHeaders.h`, which other code still uses), so this step is expected to be a no-op — verify rather than assume, and do not remove headers still needed by other code.

## Critical files

- `DataPacker/Source/FileManager.cpp` — the anonymous-namespace `AddChecked` (`:179-186`) and its sole call site inside `FileManager::MaterializeOutput` (`:445`)
- `DataPacker/Source/DiagnosticReporter.cpp` — completed precedent only (`ReportMaterializationDiskSpace`); **not modified by this plan**

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change that satisfies the acceptance criteria, and add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way.

**In scope** — only these regions of `DataPacker/Source/FileManager.cpp`:

- The anonymous-namespace function `AddChecked` (`:179-186`): delete it.
- The single statement `uiAllocation = AddChecked(...)` inside `FileManager::MaterializeOutput` (`:445`): replace with the inlined plain arithmetic from Design step 3.
- Only if the deletion actually orphans one: a local `#include`/`using` removal per Design step 4 (expected no-op).

Naming a file grants no permission to touch anything else in it. Every other line of `FileManager.cpp` — including the rest of `MaterializeOutput` — stays byte-identical.

**Out of scope**:

- The surrounding output-materialization logic: the `GetDiskFreeSpaceW` failure check, reparse-point rejection, unsupported-entry rejection, file ordering, and the disk-space decision itself all keep their current behavior.
- `DiagnosticReporter.cpp` — its guards are already gone; this plan only cites it as precedent.
- Any other DataPacker validation or error-handling audit. This plan removes one named unreachable guard, not a category sweep.
- Changing the cluster-rounding arithmetic's results. The rounding is behavior-preserving; only the throw path disappears.

## Acceptance criteria

- The unreachability verification result (Design step 1) is recorded before the edit.
- No `AddChecked` symbol remains in `DataPacker/Source/FileManager.cpp`; the string `"Output materialization size overflow"` is gone from the repository.
- `uiAllocation` at the former `AddChecked` call site accumulates the identical cluster-rounded value as before for all non-overflowing inputs (i.e. every reachable input).
- DataPacker compiles.

## Risk tier and invariants

- Risk trigger: Tier 1 — single-file, behavior-preserving on every reachable input, no public signature or invariant exposure. The deleted helper is file-local to an anonymous namespace, so it has no external callers.
- Invariant exposure: none. DataPacker is an offline tool — no game runtime, no determinism/CRC, no wire protocol, no `.pack`/`kiVersion` layout, no replay, no client/server guard, no allocation-tracked path. The `.pack` output bytes are unaffected; this touches only the pre-materialization disk-space estimate.
- Tools builds have no allocation tracker, so no LOG/allocation discipline applies.
- Verification is a DataPacker compile plus the diff itself; no live agent-harness scenario is warranted for an unreachable-branch deletion in an offline tool.
